class AsciiChat < Formula
  desc "Real-time terminal-based video chat with ASCII art conversion"
  homepage "https://github.com/zfogg/ascii-chat"
  url "https://github.com/zfogg/ascii-chat/archive/refs/tags/v0.6.12.tar.gz"
  sha256 "3498b09d9e8b645fe741e00ecd854afd2b3f273b70cfb714f5eea4259f4379a9"
  license "MIT"
  head "https://github.com/zfogg/ascii-chat.git", branch: "master"

  depends_on "cmake" => :build
  depends_on "ffmpeg" => :build
  depends_on "libsodium" => :build
  depends_on "lld" => :build
  depends_on "llvm" => :build
  depends_on "mimalloc" => :build
  depends_on "ninja" => :build
  depends_on "opus" => :build
  depends_on "portaudio" => :build
  depends_on "zstd" => :build

  depends_on "criterion" => :test

  depends_on "ca-certificates"
  depends_on "gnupg"

  on_macos do
    depends_on "pkg-config" => :build
  end

  on_linux do
    depends_on "pkg-config" => :build
  end

  def install
    # Initialize git submodules for dependencies
    system "git", "submodule", "update", "--init", "--recursive" if build.head?

    # Set up Homebrew LLVM
    ENV["CC"] = Formula["llvm"].opt_bin/"clang"
    ENV["CXX"] = Formula["llvm"].opt_bin/"clang++"
    ENV["OBJC"] = Formula["llvm"].opt_bin/"clang" if OS.mac?
    ENV["OBJCXX"] = Formula["llvm"].opt_bin/"clang++" if OS.mac?

    # Use Homebrew LLVM's bundled libunwind
    llvm_lib = Formula["llvm"].opt_lib
    ENV["LDFLAGS"] = "-L#{llvm_lib}/unwind -lunwind" if OS.mac?

    # Build defer tool from source using Homebrew's LLVM to avoid version conflicts
    # Pre-built binaries are incompatible with Homebrew's LLVM version
    # The defer tool will be built automatically by CMake if not specified

    # Get macOS SDK path if on macOS
    sdk_path = Utils.safe_popen_read("xcrun", "--show-sdk-path").chomp if OS.mac?

    # CMake configuration
    llvm_bin = Formula["llvm"].opt_bin
    cmake_args = [
      "-B", "build",
      "-S", ".",
      "-G", "Ninja",
      "-DCMAKE_BUILD_TYPE=Release",
      "-DCMAKE_INSTALL_PREFIX=#{prefix}",
      "-DUSE_MUSL=OFF",
      "-DASCIICHAT_ENABLE_ANALYZERS=OFF",
      "-DASCIICHAT_LLVM_CONFIG_EXECUTABLE=#{llvm_bin}/llvm-config",
      "-DASCIICHAT_CLANG_EXECUTABLE=#{llvm_bin}/clang",
      "-DASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE=#{llvm_bin}/clang++",
      "-DASCIICHAT_LLVM_AR_EXECUTABLE=#{llvm_bin}/llvm-ar",
      "-DASCIICHAT_LLVM_RANLIB_EXECUTABLE=#{llvm_bin}/llvm-ranlib",
      "-DASCIICHAT_LLVM_NM_EXECUTABLE=#{llvm_bin}/llvm-nm",
      "-DASCIICHAT_LLVM_READELF_EXECUTABLE=#{llvm_bin}/llvm-readelf",
      "-DASCIICHAT_LLVM_OBJDUMP_EXECUTABLE=#{llvm_bin}/llvm-objdump",
      "-DASCIICHAT_LLVM_STRIP_EXECUTABLE=#{llvm_bin}/llvm-strip",
      "-DASCIICHAT_LLD_EXECUTABLE=#{Formula["lld"].opt_bin}/ld.lld"
    ]

    # Add macOS-specific options
    if OS.mac?
      cmake_args += [
        "-DCMAKE_OSX_SYSROOT=#{sdk_path}",
        "-DCMAKE_OBJC_COMPILER=#{llvm_bin}/clang",
        "-DCMAKE_OBJCXX_COMPILER=#{llvm_bin}/clang++",
      ]
    end

    system "cmake", *cmake_args
    system "cmake", "--build", "build", "--target", "ascii-chat"
    system "cmake", "--build", "build", "--target", "man1"

    # Install binaries and documentation
    bin.install "build/bin/ascii-chat"
    man1.install "build/docs/ascii-chat.1"

    # Install shell completions
    bash_completion.install "share/completions/ascii-chat.bash" => "ascii-chat"
    zsh_completion.install "share/completions/_ascii-chat"
    fish_completion.install "share/completions/ascii-chat.fish"
  end

  service do
    run [opt_bin/"ascii-chat", "server"]
    keep_alive crashed: true
    working_dir var
    log_path var/"log/ascii-chat.log"
    error_log_path var/"log/ascii-chat.log"
  end

  test do
    # Basic functionality test
    assert_match "ascii-chat", shell_output("#{bin}/ascii-chat --help 2>&1")
    assert_match version.to_s, shell_output("#{bin}/ascii-chat --version 2>&1")

    # Test snapshot mode (doesn't require webcam)
    # Start server in background
    server_pid = fork do
      exec bin/"ascii-chat", "server", "--port", "27225"
    end

    # Give server time to start
    sleep 2

    begin
      # Connect client in snapshot mode (will capture one frame and exit)
      system bin/"ascii-chat", "client", "127.0.0.1:27225", "--snapshot", "--no-webcam"
    ensure
      # Clean up server
      Process.kill("TERM", server_pid)
      Process.wait(server_pid)
    end
  end
end
