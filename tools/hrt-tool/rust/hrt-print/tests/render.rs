use hrtprint::hrtprint_render;
use std::ffi::CString;
use std::os::raw::c_char;

fn render(json: &str, format: &str, capacity: usize) -> (i32, String, String) {
    let json = CString::new(json).unwrap();
    let format = CString::new(format).unwrap();
    let mut out = vec![0u8; capacity];
    let mut err = vec![0u8; 512];
    let mut out_len = 0usize;
    let code = hrtprint_render(
        json.as_ptr(),
        format.as_ptr(),
        out.as_mut_ptr() as *mut c_char,
        out.len(),
        &mut out_len,
        err.as_mut_ptr() as *mut c_char,
        err.len(),
    );
    let text = String::from_utf8_lossy(&out[..out_len]).to_string();
    let error = String::from_utf8_lossy(&err[..err.iter().position(|&b| b == 0).unwrap_or(0)])
        .to_string();
    (code, text, error)
}

fn view_model() -> &'static str {
    r#"{
        "now": "2026-10-20 22:50 UTC+08:00",
        "mode": "transfem",
        "weightKg": 70,
        "compounds": [
            {
                "drug": "Estradiol valerate",
                "route": "injection",
                "lastDose": "1d 11h 10m",
                "now": "169 pg/mL",
                "offsets": [{"label": "+1m", "value": "165"}, {"label": "+1h", "value": "140"}]
            }
        ],
        "labs": [
            {"time": "2026-10-18 09:00", "name": "Estradiol", "value": "180 pg/mL", "method": "LC-MS/MS"}
        ],
        "warnings": ["warning: uncalibrated baseline"]
    }"#
}

#[test]
fn renders_a_table() {
    let (code, text, error) = render(view_model(), "table", 4096);
    assert_eq!(code, 0, "{}", error);
    assert!(text.contains("Drug"));
    assert!(text.contains("Estradiol valerate"));
    assert!(text.contains("169 pg/mL"));
    assert!(text.contains("Recent labs:"));
    assert!(text.contains("uncalibrated baseline"));
}

#[test]
fn renders_json() {
    let (code, text, error) = render(view_model(), "json", 4096);
    assert_eq!(code, 0, "{}", error);
    assert!(text.contains("\"compounds\""));
}

#[test]
fn rejects_invalid_json() {
    let (code, _text, error) = render("{not json", "table", 512);
    assert_eq!(code, 1);
    assert!(!error.is_empty());
}

#[test]
fn rejects_unknown_format() {
    let (code, _text, error) = render("{}", "yaml", 512);
    assert_eq!(code, 3);
    assert!(!error.is_empty());
}

#[test]
fn rejects_a_small_buffer() {
    let (code, _text, error) = render(view_model(), "table", 8);
    assert_eq!(code, 2);
    assert!(!error.is_empty());
}
