use std::{net::IpAddr, sync::Arc, time::Duration};

use axum::Router;
use tokio::{net::TcpListener, task::JoinHandle};
use tokio_util::sync::CancellationToken;
use tracing::{info, warn};
use tracing_subscriber::{EnvFilter, fmt, layer::SubscriberExt, util::SubscriberInitExt};

use crate::{
    config::Settings,
    error::AppError,
    http,
    infrastructure::{self, CdnClient},
};

#[derive(Clone)]
pub struct AppState {
    pub settings: Arc<Settings>,
    pub cdn: CdnClient,
}

pub async fn run() -> Result<(), AppError> {
    dotenvy::dotenv().ok();
    init_tracing();

    let settings = Arc::new(Settings::from_env()?);
    let cdn = infrastructure::connect(&settings)?;
    let state = AppState {
        settings: Arc::clone(&settings),
        cdn,
    };

    let shutdown = CancellationToken::new();
    let app = http::router(state.clone());
    let result = serve(
        app,
        settings.host,
        settings.port,
        settings.cdn_url.clone(),
        shutdown.clone(),
    )
    .await;

    shutdown.cancel();
    wait_for_background_tasks(Vec::new(), settings.shutdown_timeout).await;

    result
}

async fn serve(
    app: Router,
    host: IpAddr,
    port: u16,
    cdn_url: String,
    shutdown: CancellationToken,
) -> Result<(), AppError> {
    let listener = TcpListener::bind((host, port)).await?;
    info!(%host, %port, %cdn_url, "file router listening");

    axum::serve(listener, app)
        .with_graceful_shutdown(shutdown_signal(shutdown))
        .await?;

    Ok(())
}

fn init_tracing() {
    let filter = EnvFilter::try_from_default_env()
        .unwrap_or_else(|_| EnvFilter::new("file_router=info,tower_http=info"));

    let _ = tracing_subscriber::registry()
        .with(filter)
        .with(fmt::layer().compact())
        .try_init();
}

async fn shutdown_signal(shutdown: CancellationToken) {
    let ctrl_c = async {
        tokio::signal::ctrl_c()
            .await
            .expect("failed to install Ctrl+C handler");
    };

    #[cfg(unix)]
    let terminate = async {
        tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
            .expect("failed to install SIGTERM handler")
            .recv()
            .await;
    };

    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();

    tokio::select! {
        _ = ctrl_c => {},
        _ = terminate => {},
    }

    info!("received shutdown signal");
    shutdown.cancel();
}

async fn wait_for_background_tasks(handles: Vec<JoinHandle<()>>, timeout: Duration) {
    let joined = async {
        for handle in handles {
            if let Err(error) = handle.await {
                warn!(%error, "background task join failed");
            }
        }
    };

    if tokio::time::timeout(timeout, joined).await.is_err() {
        warn!(?timeout, "background task shutdown timed out");
    }
}
