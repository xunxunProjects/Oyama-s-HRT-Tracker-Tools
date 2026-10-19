use serde_json::Value;
use std::ffi::CStr;
use std::os::raw::{c_char, c_int};

static VERSION: &[u8] = b"0.1.0\0";

const MAX_INPUT_BYTES: usize = 16 * 1024 * 1024;
const MAX_OUTPUT_BYTES: usize = 16 * 1024 * 1024;

/// Version of the Rust printing library, as a NUL-terminated C string.
#[no_mangle]
pub extern "C" fn hrtprint_version() -> *const c_char {
    VERSION.as_ptr().cast()
}

fn write_err(err: *mut c_char, err_cap: usize, message: &str) {
    if err.is_null() || err_cap == 0 {
        return;
    }
    let bytes = message.as_bytes();
    let count = bytes.len().min(err_cap - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), err as *mut u8, count);
        *err.add(count) = 0;
    }
}

fn render_table(value: &Value) -> Result<String, String> {
    let object = value
        .as_object()
        .ok_or_else(|| "view model is not an object".to_string())?;

    let mut out = String::new();
    if let Some(now) = object.get("now").and_then(Value::as_str) {
        out.push_str(&format!("Now      {}\n", now));
    }
    if let Some(mode) = object.get("mode").and_then(Value::as_str) {
        out.push_str(&format!("Mode     {}\n", mode));
    }
    if let Some(weight) = object.get("weightKg") {
        out.push_str(&format!("Weight   {} kg\n", weight));
    }

    if let Some(compounds) = object.get("compounds").and_then(Value::as_array) {
        if !compounds.is_empty() {
            let labels: Vec<String> = compounds
                .first()
                .and_then(|compound| compound.get("offsets"))
                .and_then(Value::as_array)
                .map(|offsets| {
                    offsets
                        .iter()
                        .filter_map(|offset| {
                            offset
                                .get("label")
                                .and_then(Value::as_str)
                                .map(str::to_string)
                        })
                        .collect()
                })
                .unwrap_or_default();

            let drug_width = compounds
                .iter()
                .filter_map(|compound| compound.get("drug").and_then(Value::as_str))
                .map(str::len)
                .max()
                .unwrap_or(0)
                .max("Drug".len());
            let route_width = compounds
                .iter()
                .filter_map(|compound| compound.get("route").and_then(Value::as_str))
                .map(str::len)
                .max()
                .unwrap_or(0)
                .max("Route".len());
            let last_width = compounds
                .iter()
                .filter_map(|compound| compound.get("lastDose").and_then(Value::as_str))
                .map(str::len)
                .max()
                .unwrap_or(0)
                .max("Last dose".len());
            let now_width = compounds
                .iter()
                .filter_map(|compound| compound.get("now").and_then(Value::as_str))
                .map(str::len)
                .max()
                .unwrap_or(0)
                .max("Now".len());

            out.push('\n');
            out.push_str(&format!(
                "{:<drug_width$}  {:<route_width$}  {:<last_width$}  {:<now_width$}",
                "Drug", "Route", "Last dose", "Now"
            ));
            for label in &labels {
                out.push_str(&format!("  {:>8}", label));
            }
            out.push('\n');

            for compound in compounds {
                let drug = compound.get("drug").and_then(Value::as_str).unwrap_or("");
                let route = compound.get("route").and_then(Value::as_str).unwrap_or("");
                let last = compound
                    .get("lastDose")
                    .and_then(Value::as_str)
                    .unwrap_or("");
                let now_value = compound.get("now").and_then(Value::as_str).unwrap_or("");
                out.push_str(&format!(
                    "{:<drug_width$}  {:<route_width$}  {:<last_width$}  {:<now_width$}",
                    drug, route, last, now_value
                ));
                if let Some(offsets) = compound.get("offsets").and_then(Value::as_array) {
                    for offset in offsets {
                        let value = offset.get("value").and_then(Value::as_str).unwrap_or("");
                        out.push_str(&format!("  {:>8}", value));
                    }
                }
                out.push('\n');
            }
        }
    }

    if let Some(labs) = object.get("labs").and_then(Value::as_array) {
        if !labs.is_empty() {
            out.push_str("\nRecent labs:\n");
            for lab in labs {
                let time = lab.get("time").and_then(Value::as_str).unwrap_or("");
                let name = lab.get("name").and_then(Value::as_str).unwrap_or("");
                let value = lab.get("value").and_then(Value::as_str).unwrap_or("");
                let method = lab.get("method").and_then(Value::as_str).unwrap_or("");
                out.push_str(&format!("  {}  {}  {}", time, name, value));
                if !method.is_empty() {
                    out.push_str(&format!("  {}", method));
                }
                out.push('\n');
            }
        }
    }

    if let Some(warnings) = object.get("warnings").and_then(Value::as_array) {
        for warning in warnings {
            if let Some(text) = warning.as_str() {
                out.push_str(text);
                out.push('\n');
            }
        }
    }

    Ok(out)
}

/// Render a JSON view model.
///
/// Returns 0 on success, 1 for invalid JSON, 2 for a too-small output buffer,
/// 3 for an unknown format, 4 for oversized input, and 5 for null pointers.
#[no_mangle]
pub extern "C" fn hrtprint_render(
    json: *const c_char,
    format: *const c_char,
    out: *mut c_char,
    out_cap: usize,
    out_len: *mut usize,
    err: *mut c_char,
    err_cap: usize,
) -> c_int {
    if json.is_null() || format.is_null() || out.is_null() || out_len.is_null() {
        write_err(err, err_cap, "null pointer");
        return 5;
    }

    let json_text = match unsafe { CStr::from_ptr(json) }.to_str() {
        Ok(text) => text,
        Err(_) => {
            write_err(err, err_cap, "input is not valid UTF-8");
            return 1;
        }
    };
    if json_text.len() > MAX_INPUT_BYTES {
        write_err(err, err_cap, "input exceeds 16 MiB");
        return 4;
    }
    let format_text = match unsafe { CStr::from_ptr(format) }.to_str() {
        Ok(text) => text,
        Err(_) => {
            write_err(err, err_cap, "format is not valid UTF-8");
            return 3;
        }
    };

    let value: Value = match serde_json::from_str(json_text) {
        Ok(value) => value,
        Err(error) => {
            write_err(err, err_cap, &format!("invalid JSON: {}", error));
            return 1;
        }
    };

    let rendered = match format_text {
        "json" => match serde_json::to_string_pretty(&value) {
            Ok(text) => text,
            Err(error) => {
                write_err(err, err_cap, &format!("cannot render JSON: {}", error));
                return 3;
            }
        },
        "table" => match render_table(&value) {
            Ok(text) => text,
            Err(error) => {
                write_err(err, err_cap, &error);
                return 3;
            }
        },
        _ => {
            write_err(err, err_cap, "unknown format");
            return 3;
        }
    };

    if rendered.len() > MAX_OUTPUT_BYTES || rendered.len() + 1 > out_cap {
        write_err(err, err_cap, "output buffer is too small");
        return 2;
    }

    unsafe {
        std::ptr::copy_nonoverlapping(rendered.as_ptr(), out as *mut u8, rendered.len());
        *out.add(rendered.len()) = 0;
        *out_len = rendered.len();
    }
    write_err(err, err_cap, "");
    0
}
