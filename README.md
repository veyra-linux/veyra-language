# VEYRA Programming Language

**VEYRA 1.0.0** is a Linux-focused programming language using the `.vey` extension, with an interpreter written in C++20. This first 1.0 release establishes a compact core for learning programming, small programs, and read-only Linux system inspection.

> **Status:** first 1.0 language release; development continues. VEYRA is not a certified security product or a sandbox. Do not execute untrusted programs or run the interpreter as root unless necessary.

## Features

- `let` variables and `const` bindings. For arrays, `const` prevents rebinding but does not deeply freeze the array.
- Numbers, strings, booleans, `none`, and mutable arrays.
- Arithmetic, comparisons, and short-circuit logical operators.
- `if/else`, `while`, C-style `for`, `break`, and `continue`.
- User functions (`fn`), parameters, `return`, and recursion.
- Block scopes, nested arrays, indexing, and element assignment.
- Standard helpers under `text.*`, `math.*`, and `list.*`.
- Linux system information and selected filesystem metadata checks.
- `--check`, `--builtins`, and resource limits.
- Regression tests and Linux CI.

## Requirements

Linux, a C++20 compiler (GCC recommended), `make`, and OpenSSL development headers for SHA-256.

Arch Linux / Veyra Linux:

```bash
sudo pacman -S --needed base-devel openssl
```

Debian / Ubuntu:

```bash
sudo apt-get update
sudo apt-get install build-essential libssl-dev
```

## Build and test

```bash
git clone https://github.com/veyra-linux/veyra-language.git
cd veyra-language
make clean
make -j2
make test
```

A successful test run ends with `VEYRA tests: OK`.

Optional install:

```bash
sudo make install
veyra --version
```

## Quick start

Save as `hello.vey`:

```vey
const language = "VEYRA";
let name = "Abduali";

fn square(n) {
    return n * n;
}

print "Hello, " + name + "!";
print "Language: " + language;
print "7 squared = " + square(7);
print "Kernel: " + linux.kernel();
```

Run with `./veyra hello.vey`.

## Arrays

Arrays are zero-indexed, mutable, may contain mixed value types, and can be nested. The current limit is 100,000 elements per array.

```vey
let numbers = [10, 20, 30];
numbers[1] = 25;
list.push(numbers, 40);

print numbers;                    # [10, 25, 30, 40]
print numbers[0];                 # 10
print list.len(numbers);          # 4
print list.get(numbers, 2);       # 30
print list.pop(numbers);          # 40

let matrix = [[1, 2], [3, 4]];
matrix[1][0] = 9;
print matrix;                     # [[1, 2], [9, 4]]
```

Array access rejects negative, fractional, and out-of-range indexes. `list.push` returns the new length; `list.pop` returns the removed value and rejects an empty array; `list.set` returns the assigned value; `list.clear` empties an array and returns `none`. Cyclic array references are rejected.

Built-ins: `list.len(array)`, `list.push(array, value)`, `list.pop(array)`, `list.get(array, index)`, `list.set(array, index, value)`, and `list.clear(array)`.

## Language basics

```vey
let total = 0;
for (let i = 0; i < 5; i = i + 1) {
    if (i == 2) { continue; }
    total = total + i;
}
print "Total: " + total;

fn factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}
print factorial(5);
```

Comments begin with `#` or `//`. Identifiers are ASCII and case-sensitive. Strings are byte strings: `text.len` counts bytes and case conversion is ASCII-only. Lexical closures are not implemented.

## Built-in groups

Run `./veyra --builtins` for the exact list.

- `text.*`: byte length, ASCII case conversion, trim, substring, prefix, and suffix checks.
- `math.*`: abs, floor, ceil, round, sqrt, min, max, pow.
- `list.*`: array operations.
- `linux.*`: OS name, kernel release, architecture, hostname, PID, page size.
- `system.cwd()`: current working directory.
- `security.*`: UID/root checks, path accessibility, file metadata, basic permission-bit audit, and SHA-256.

`security.sha256(path)` only hashes regular files, does not follow a final symbolic link, and limits reads to 256 MiB.

## CLI

```text
veyra <file.vey>       Run a program
veyra --check <file>   Check syntax without executing
veyra --builtins       List built-in functions
veyra --version        Show version
veyra --help           Show help and limits
```

`--check` is a parser check, not a full static type checker.

## Security model

VEYRA is **not a sandbox**. Programs inherit the permissions of the user running the interpreter. The standard library has no general shell-execution, arbitrary file-write, or network API, but it can inspect system information and filesystem metadata and read files for hashing. Run only trusted programs. Use a separate unprivileged account and operating-system isolation for untrusted code.

The official runtime is Linux-only, but `.vey` is plain text; third parties can write another interpreter or run Linux in a VM/WSL.

## Roadmap

Potential future work includes modules/imports, stronger static analysis, a richer type system, native-code compilation, improved diagnostics, fuzz testing, and a dedicated sandbox runner. These are not features of 1.0.0 unless implemented in a later release.

## Contributing

Bug reports, documentation fixes, focused improvements, and tests are welcome. Include your Linux distribution, compiler version, reproduction steps, and `make test` output. Do not include secrets or sensitive system data.

## License

MIT. See [`LICENSE`](LICENSE).

---

**VEYRA — Independent by vision. Engineered with purpose.**  
Made by Abduali.
