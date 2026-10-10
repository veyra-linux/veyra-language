# VEYRA Programming Language

**VEYRA** is an experimental, Linux-focused programming language with the `.vey` file extension. Its interpreter is written in **C++20**. The project aims to make basic programming approachable while providing a small set of read-only Linux system-inspection and security-audit functions.

> **Status: experimental prototype — version 0.2.0.** VEYRA is not a certified security product and is not a sandbox. Do not use it as the only protection for important systems.

## Features

- Simple syntax with `let`, `const`, `print`, `if` / `else`, `while`, and `for`.
- User-defined functions with parameters, `return`, and recursion.
- `break`, `continue`, arithmetic, comparisons, and short-circuit logical operators.
- Built-in string and mathematics helpers under `text.*` and `math.*`.
- Linux system information through `linux.*` and `system.*` functions.
- Read-only file metadata checks and SHA-256 hashing through `security.*` functions.
- Syntax-check mode (`--check`) that parses a file without executing it.
- Runtime limits for source size, token count, parser depth, loops, instructions, expressions, strings, output, and file hashing.
- Build hardening flags enabled by the default Makefile where supported by the toolchain.

## Requirements

- A Linux x86-64 system (the included prebuilt binary targets Linux x86-64).
- A C++ compiler with C++20 support, such as GCC.
- `make` and the standard build tools.
- OpenSSL development libraries for SHA-256 (`libcrypto`).

On Arch Linux / Veyra Linux, install the dependencies with:

```bash
sudo pacman -S --needed base-devel openssl
```

## Build from source

From the project directory, run:

```bash
make clean
make -j2
make test
```

Run the tests after building. A successful test run ends with `VEYRA tests: OK`.

To optionally install the executable into `/usr/local/bin`:

```bash
sudo make install
veyra --version
```

To install somewhere else, set `PREFIX`, for example `make PREFIX="$HOME/.local" install`.

## Quick start

Create a file named `hello.vey`:

```vey
const language = "VEYRA";
let name = "Abduali";

fn square(n) {
    return n * n;
}

print "Hello, " + name + "!";
print language + " runs on " + linux.os();
print "7 squared = " + square(7);
print "Kernel: " + linux.kernel();
```

Run it with:

```bash
./veyra hello.vey
```

More examples are available in the [`examples/`](examples/) directory:

```bash
./veyra examples/hello.vey
./veyra examples/basics.vey
./veyra examples/advanced.vey
./veyra examples/factorial.vey
./veyra examples/security_audit.vey
```

## Command-line options

```text
veyra <file.vey>       Run a VEYRA program
veyra --check <file>   Check lexical and syntax correctness without execution
veyra --builtins       List built-in functions
veyra --version        Print the runtime version
veyra --help           Show help
```

`--check` currently checks lexical and syntax parsing. It does not provide a complete static type checker or guarantee that every name used by the program exists.

## Language overview

### Variables and control flow

```vey
const limit = 8;
let total = 0;

for (let i = 0; i < limit; i = i + 1) {
    if (i == 2) {
        continue;
    }
    if (i == 6) {
        break;
    }
    total = total + i;
}

if (total > 0) {
    print "Total: " + total;
} else {
    print "No total";
}
```

### Functions and strings

```vey
fn greet(name) {
    return "Hello, " + text.trim(name);
}

print greet("  VEYRA  ");
print text.upper("linux");
print math.sqrt(81);
```

Source comments begin with `#` or `//`. Semicolons are optional after simple statements, but required as separators in a `for` loop header. Identifiers are case-sensitive. The complete current language notes are in [`LANGUAGE_SPEC_RU.md`](LANGUAGE_SPEC_RU.md) (Russian).

## Built-in functions

Use `./veyra --builtins` to print the exact built-in list supported by the current executable.

### Text helpers

- `text.len(s)` — string length in bytes.
- `text.lower(s)` / `text.upper(s)` — ASCII case conversion.
- `text.trim(s)` — trim whitespace from both ends.
- `text.contains(s, part)` — check whether a substring is present.
- `text.starts_with(s, prefix)` / `text.ends_with(s, suffix)` — prefix/suffix checks.

String functions currently operate on byte strings; length is not counted in Unicode code points, and case conversion is ASCII-only.

### Mathematics

Available functions include `math.abs`, `math.floor`, `math.ceil`, `math.round`, `math.sqrt`, `math.min`, `math.max`, and `math.pow`. Invalid numeric results, such as a square root of a negative number or non-finite arithmetic results, produce an error.

### Linux and system information

Functions include `linux.os()`, `linux.kernel()`, `linux.arch()`, `linux.hostname()`, `linux.pid()`, `linux.page_size()`, and `system.cwd()`.

### Security and file inspection

The current runtime provides functions for checking effective root status and UID, querying file existence/accessibility, inspecting file type and mode bits, checking selected risky permission bits, and generating SHA-256 hashes. For example, `security.audit(path)` summarizes selected metadata and `security.sha256(path)` hashes a regular file up to 256 MiB.

The SHA-256 implementation uses OpenSSL EVP. It opens files without following a final symbolic link, checks the opened descriptor, and rejects non-regular files. These measures reduce some risks but do not eliminate all filesystem race conditions or make execution safe for untrusted code.

## Runtime limits

The current prototype applies limits including:

| Resource | Limit |
| --- | ---: |
| Source file size | 4 MiB |
| Tokens | 250,000 |
| Parser nesting depth | 256 |
| User-function call depth | 256 |
| Loop iterations | 1,000,000 |
| Executed instructions | 5,000,000 |
| Expression evaluations | 20,000,000 |
| String size | 4 MiB |
| Output | 16 MiB |
| File size accepted by `security.sha256` | 256 MiB |

These are interpreter resource limits, not a complete operating-system security boundary.

## Linux-only runtime

The official runtime source has a compile-time guard requiring `__linux__` and uses Linux-specific APIs. The official executable is a native Linux ELF binary and is not a native Windows `.exe`.

The `.vey` source file itself is plain text. This project cannot prevent someone from writing a separate interpreter, porting the implementation, or running Linux through a virtual machine or WSL. The Linux-only restriction applies to the official VEYRA runtime, not to the text file format in an absolute sense.

## Security notice

**VEYRA is not a sandbox.** A VEYRA program runs with the permissions of the user who launches it. The current language does not provide built-in shell-command execution, network access, or arbitrary file-writing functions, but it can inspect filesystem metadata and read/hash files available to that user. Never run untrusted `.vey` programs, and avoid running VEYRA with `sudo` unless it is specifically required.

For real isolation, use a separate unprivileged account and operating-system controls such as namespaces, seccomp, and filesystem restrictions. VEYRA has not undergone an independent security audit and should not be the sole security control for critical systems.

## Project roadmap

Potential future work includes arrays and maps, a module/import system, stronger static analysis and type checking, fuzz testing, a dedicated sandbox runner, and a compiler that emits native Linux ELF executables. These are goals for future development, not features of version 0.2.0.

## Contributing

Bug reports, focused improvements, documentation fixes, and tests are welcome. Please include your Linux distribution, compiler version, reproduction steps, and the output of `make test` when reporting a problem. Do not submit secrets, private system logs, or sensitive file contents.

## License

VEYRA is released under the [MIT License](LICENSE).

---

**VEYRA — an independent Linux-focused language experiment.**

Made by Abduali.
