use futures_util::Stream;
use http::header::CONTENT_TYPE;
use reqwest::{Client, StatusCode};
use thiserror::Error;

use crate::{config::Settings, error::ConfigError};

#[derive(Clone)]
pub struct CdnClient {
    client: Client,
    base_url: String,
}

pub struct ImageResponse {
    pub content_type: String,
    pub content_length: u64,
    pub body: reqwest::Response,
}

#[derive(Debug, Error)]
pub enum CdnError {
    #[error("image not found")]
    NotFound,

    #[error("upstream response was not an image")]
    NotImage,

    #[error("upstream response missing content length")]
    MissingContentLength,

    #[error("CDN transport error: {0}")]
    Transport(#[from] reqwest::Error),
}

pub fn connect(settings: &Settings) -> Result<CdnClient, ConfigError> {
    CdnClient::new(settings)
}

impl CdnClient {
    fn new(settings: &Settings) -> Result<Self, ConfigError> {
        let client = Client::builder()
            .timeout(settings.request_timeout)
            .pool_max_idle_per_host(32)
            .build()
            .map_err(ConfigError::HttpClient)?;

        Ok(Self {
            client,
            base_url: settings.cdn_url.clone(),
        })
    }

    pub async fn fetch_image(&self, path: &str) -> Result<ImageResponse, CdnError> {
        let url = format!("{}/u/{path}", self.base_url);
        let response = self.client.get(url).send().await?;

        if response.status() != StatusCode::OK {
            return Err(CdnError::NotFound);
        }

        let content_type = response
            .headers()
            .get(CONTENT_TYPE)
            .and_then(|value| value.to_str().ok())
            .unwrap_or_default()
            .to_owned();

        if !content_type.starts_with("image/") {
            return Err(CdnError::NotImage);
        }

        let content_length = response.content_length().filter(|len| *len > 0).ok_or(
            CdnError::MissingContentLength,
        )?;

        Ok(ImageResponse {
            content_type,
            content_length,
            body: response,
        })
    }
}

impl ImageResponse {
    pub fn into_body_stream(self) -> impl Stream<Item = Result<bytes::Bytes, reqwest::Error>> + Send {
        self.body.bytes_stream()
    }
}
