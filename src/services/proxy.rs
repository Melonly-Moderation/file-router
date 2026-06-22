use crate::{
    domain::has_known_extension,
    infrastructure::cdn::{CdnClient, CdnError, ImageResponse},
};

const EXTENSIONS: [&str; 3] = [".webp", ".png", ".jpg"];

pub async fn fetch_with_extensions(cdn: &CdnClient, id: &str) -> Result<ImageResponse, CdnError> {
    if has_known_extension(id) {
        return cdn.fetch_image(id).await;
    }

    if let Ok(image) = cdn.fetch_image(id).await {
        return Ok(image);
    }

    for extension in EXTENSIONS {
        if let Ok(image) = cdn.fetch_image(&format!("{id}{extension}")).await {
            return Ok(image);
        }
    }

    Err(CdnError::NotFound)
}
