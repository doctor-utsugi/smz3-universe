# The browser build's tools: Emscripten, Python with Pillow for the asset scripts, and the Rust that core/maprando pins,
# with std's sources so std can be rebuilt with atomics for wasm32-unknown-emscripten.
FROM emscripten/emsdk:4.0.20
RUN apt-get update && apt-get install -y --no-install-recommends python3-pil && rm -rf /var/lib/apt/lists/*
ENV RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo PATH=/opt/cargo/bin:$PATH
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain 1.90.0 \
 && rustup component add rust-src --toolchain 1.90.0 \
 && rustup target add wasm32-unknown-emscripten --toolchain 1.90.0 \
 && chmod -R a+rwX /opt/rustup /opt/cargo
