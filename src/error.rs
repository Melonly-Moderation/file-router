use std::{net::AddrParseError, num::ParseIntError};

use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
};
use thiserror::Error;
use tracing::error;

use crate::infrastructure::cdn::CdnError;

pub type AppResult<T> = Result<T, AppError>;

#[derive(Debug, Error)]
pub enum AppError {
    #[error(transparent)]
    Config(#[from] ConfigError),

    #[error(transparent)]
    Cdn(#[from] CdnError),

    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
}

impl IntoResponse for AppError {
    fn into_response(self) -> Response {
        error!(error = %self, "server error");
        StatusCode::INTERNAL_SERVER_ERROR.into_response()
    }
}

#[derive(Debug, Error)]
pub enum ConfigError {
    #[error("invalid integer: {0}")]
    ParseInt(#[from] ParseIntError),

    #[error("invalid IP address: {0}")]
    Address(#[from] AddrParseError),

    #[error("environment variable {0} must be greater than zero seconds")]
    InvalidDuration(&'static str),

    #[error("invalid CDN URL: {0}")]
    InvalidCdnUrl(String),

    #[error("failed to build HTTP client: {0}")]
    HttpClient(#[source] reqwest::Error),
}
