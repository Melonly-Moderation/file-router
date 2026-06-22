#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PathError {
    Invalid,
}

pub fn validate_id(id: &str, max_len: usize) -> Result<(), PathError> {
    let len = id.len();
    if len == 0 || len > max_len {
        return Err(PathError::Invalid);
    }

    if id.contains("..") {
        return Err(PathError::Invalid);
    }

    if !id.bytes().all(|byte| {
        byte.is_ascii_alphanumeric() || byte == b'-' || byte == b'_' || byte == b'/' || byte == b'.'
    }) {
        return Err(PathError::Invalid);
    }

    Ok(())
}

pub fn has_known_extension(id: &str) -> bool {
    id.contains('/')
        || id.ends_with(".webp")
        || id.ends_with(".png")
        || id.ends_with(".jpg")
        || id.ends_with(".jpeg")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_empty_and_traversal() {
        assert_eq!(validate_id("", 100), Err(PathError::Invalid));
        assert_eq!(validate_id("foo/../bar", 100), Err(PathError::Invalid));
    }

    #[test]
    fn accepts_valid_ids() {
        assert!(validate_id("abc-123/file.webp", 100).is_ok());
    }
}
