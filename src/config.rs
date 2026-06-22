use std::{env, net::IpAddr, num::ParseIntError, time::Duration};

use crate::error::ConfigError;

const DEFAULT_HOST: &str = "0.0.0.0";
const DEFAULT_PORT: &str = "8080";
const DEFAULT_CDN_URL: &str = "http://cdn_zipline:3000";
const DEFAULT_MAX_ID_LENGTH: usize = 100;
const DEFAULT_REQUEST_TIMEOUT_SECONDS: u64 = 10;
const DEFAULT_SHUTDOWN_TIMEOUT_SECONDS: u64 = 10;

#[derive(Debug, Clone)]
pub struct Settings {
    pub host: IpAddr,
    pub port: u16,
    pub cdn_url: String,
    pub max_id_length: usize,
    pub request_timeout: Duration,
    pub shutdown_timeout: Duration,
}

impl Settings {
    pub fn from_env() -> Result<Self, ConfigError> {
        Ok(Self {
            host: read_env_or("HOST", DEFAULT_HOST)?.parse()?,
            port: read_env_or("PORT", DEFAULT_PORT)?.parse()?,
            cdn_url: trim_url(&read_env_or("CDN_URL", DEFAULT_CDN_URL)?),
            max_id_length: read_usize_or("MAX_ID_LENGTH", DEFAULT_MAX_ID_LENGTH)?.max(1),
            request_timeout: read_duration_or(
                "REQUEST_TIMEOUT_SECONDS",
                DEFAULT_REQUEST_TIMEOUT_SECONDS,
            )?,
            shutdown_timeout: read_duration_or(
                "SHUTDOWN_TIMEOUT_SECONDS",
                DEFAULT_SHUTDOWN_TIMEOUT_SECONDS,
            )?,
        })
    }
}

fn read_env_or(key: &'static str, fallback: &'static str) -> Result<String, ConfigError> {
    Ok(env::var(key)
        .ok()
        .map(|value| value.trim().to_owned())
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| fallback.to_owned()))
}

fn read_usize_or(key: &'static str, fallback: usize) -> Result<usize, ParseIntError> {
    Ok(env::var(key)
        .ok()
        .map(|value| value.trim().to_owned())
        .filter(|value| !value.is_empty())
        .map(|value| value.parse())
        .transpose()?
        .unwrap_or(fallback))
}

fn read_duration_or(key: &'static str, fallback_seconds: u64) -> Result<Duration, ConfigError> {
    let seconds = env::var(key)
        .ok()
        .map(|value| value.trim().to_owned())
        .filter(|value| !value.is_empty())
        .map(|value| value.parse())
        .transpose()?
        .unwrap_or(fallback_seconds);

    if seconds == 0 {
        return Err(ConfigError::InvalidDuration(key));
    }

    Ok(Duration::from_secs(seconds))
}

fn trim_url(value: &str) -> String {
    value.trim().trim_end_matches('/').to_owned()
}
