#[tokio::main]
async fn main() -> Result<(), file_router::error::AppError> {
    file_router::app::run().await
}
