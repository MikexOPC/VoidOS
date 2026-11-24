// ====== main ======

fn main() {
    let source = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../userspace/gui/ui/desktop.slint");
    let config = slint_build::CompilerConfiguration::new()
        .embed_resources(slint_build::EmbedResourcesKind::EmbedForSoftwareRenderer);
    slint_build::compile_with_config(source, config).expect("compile production desktop");
}
