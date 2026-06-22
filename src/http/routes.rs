use axum::{
    Router,
    body::Body,
    extract::{Path, State},
    http::{HeaderValue, StatusCode, header},
    response::{IntoResponse, Response},
    routing::get,
};
use tower_http::{
    cors::{Any, CorsLayer},
    trace::TraceLayer,
};

use crate::{
    app::AppState,
    domain::validate_id,
    services::fetch_with_extensions,
};

pub fn router(state: AppState) -> Router {
    Router::new()
        .route("/{*path}", get(proxy_image))
        .layer(cors())
        .layer(TraceLayer::new_for_http())
        .with_state(state)
}

async fn proxy_image(State(state): State<AppState>, Path(path): Path<String>) -> Response {
    if validate_id(&path, state.settings.max_id_length).is_err() {
        return not_found();
    }

    let image = match fetch_with_extensions(&state.cdn, &path).await {
        Ok(image) => image,
        Err(_) => return not_found(),
    };

    let mut headers = axum::http::HeaderMap::new();
    headers.insert(
        header::CONTENT_TYPE,
        HeaderValue::from_str(&image.content_type).unwrap_or(HeaderValue::from_static("application/octet-stream")),
    );
    headers.insert(
        header::CONTENT_LENGTH,
        HeaderValue::from(image.content_length),
    );
    headers.insert(
        header::CACHE_CONTROL,
        HeaderValue::from_static("public, max-age=3600"),
    );
    headers.insert(
        header::ACCESS_CONTROL_ALLOW_ORIGIN,
        HeaderValue::from_static("*"),
    );

    let body = Body::from_stream(image.into_body_stream());

    (StatusCode::OK, headers, body).into_response()
}

fn not_found() -> Response {
    (
        StatusCode::NOT_FOUND,
        [(header::CONTENT_LENGTH, HeaderValue::from_static("0"))],
    )
        .into_response()
}

fn cors() -> CorsLayer {
    CorsLayer::new()
        .allow_origin(Any)
        .allow_methods([axum::http::Method::GET, axum::http::Method::OPTIONS])
}
