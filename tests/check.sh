#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

./veyra --version | grep -q 'VEYRA 1.0.0'
./veyra --builtins | grep -q 'list.push'
./veyra --check examples/hello.vey | grep -q 'синтаксис OK'
./veyra examples/hello.vey | grep -q 'Привет! Это язык VEYRA.'
./veyra examples/basics.vey | grep -q 'Квадрат 7 = 49'
./veyra examples/security_audit.vey | grep -q 'SHA-256 /etc/passwd:'
./veyra examples/advanced.vey | grep -q 'Сумма = 13'
./veyra examples/factorial.vey | grep -q '5! = 120'
./veyra examples/arrays.vey | grep -Fq 'Nested: [[1, 2], [9, 4]]'
./veyra examples/arrays.vey | grep -Fq 'After mutation: [10, 25, 30, 40]'

cat > "$tmpdir/loop.vey" <<'VEY'
while (true) { print "loop"; }
VEY
if ./veyra "$tmpdir/loop.vey" >/dev/null 2>&1; then
  echo 'FAIL: loop budget was not enforced' >&2; exit 1
fi

cat > "$tmpdir/hash-device.vey" <<'VEY'
print security.sha256("/dev/zero");
VEY
if ./veyra "$tmpdir/hash-device.vey" >/dev/null 2>&1; then
  echo 'FAIL: hashing a device was not blocked' >&2; exit 1
fi

ln -s /etc/passwd "$tmpdir/passwd-link"
cat > "$tmpdir/hash-link.vey" <<VEY
print security.sha256("$tmpdir/passwd-link");
VEY
if ./veyra "$tmpdir/hash-link.vey" >/dev/null 2>&1; then
  echo 'FAIL: hashing a symbolic link was not blocked' >&2; exit 1
fi

cat > "$tmpdir/constant.vey" <<'VEY'
const fixed = 10;
fixed = 11;
VEY
if ./veyra "$tmpdir/constant.vey" >/dev/null 2>&1; then
  echo 'FAIL: const value was mutable' >&2; exit 1
fi

cat > "$tmpdir/break-outside.vey" <<'VEY'
break;
VEY
if ./veyra "$tmpdir/break-outside.vey" >/dev/null 2>&1; then
  echo 'FAIL: break outside loop was accepted' >&2; exit 1
fi

cat > "$tmpdir/scope.vey" <<'VEY'
if (true) { let local_value = 1; }
print local_value;
VEY
if ./veyra "$tmpdir/scope.vey" >/dev/null 2>&1; then
  echo 'FAIL: block-local variable leaked outside scope' >&2; exit 1
fi

opens="$(printf '%300s' '' | tr ' ' '(')"
closes="$(printf '%300s' '' | tr ' ' ')')"
printf 'print %s1%s;\n' "$opens" "$closes" > "$tmpdir/deep.vey"
if ./veyra "$tmpdir/deep.vey" >/dev/null 2>&1; then
  echo 'FAIL: parser nesting limit was not enforced' >&2; exit 1
fi

cat > "$tmpdir/math-domain.vey" <<'VEY'
print math.sqrt(-1);
VEY
if ./veyra "$tmpdir/math-domain.vey" >/dev/null 2>&1; then
  echo 'FAIL: invalid square root was accepted' >&2; exit 1
fi

cat > "$tmpdir/array-bounds.vey" <<'VEY'
let xs = [1, 2];
print xs[2];
VEY
if ./veyra "$tmpdir/array-bounds.vey" >/dev/null 2>&1; then
  echo 'FAIL: out-of-bounds array access was accepted' >&2; exit 1
fi

cat > "$tmpdir/array-fractional-index.vey" <<'VEY'
let xs = [1, 2];
print xs[0.5];
VEY
if ./veyra "$tmpdir/array-fractional-index.vey" >/dev/null 2>&1; then
  echo 'FAIL: fractional array index was accepted' >&2; exit 1
fi

cat > "$tmpdir/array-pop-empty.vey" <<'VEY'
let xs = [];
list.pop(xs);
VEY
if ./veyra "$tmpdir/array-pop-empty.vey" >/dev/null 2>&1; then
  echo 'FAIL: popping an empty array was accepted' >&2; exit 1
fi

cat > "$tmpdir/array-self-cycle.vey" <<'VEY'
let xs = [];
list.push(xs, xs);
print xs;
VEY
if ./veyra "$tmpdir/array-self-cycle.vey" >/dev/null 2>&1; then
  echo 'FAIL: cyclic array creation was accepted' >&2; exit 1
fi

cat > "$tmpdir/array-nested-cycle.vey" <<'VEY'
let a = [];
let b = [a];
list.push(a, b);
VEY
if ./veyra "$tmpdir/array-nested-cycle.vey" >/dev/null 2>&1; then
  echo 'FAIL: indirect cyclic array creation was accepted' >&2; exit 1
fi

cat > "$tmpdir/array-list-set.vey" <<'VEY'
let xs = [1, 2];
list.set(xs, 1, 42);
print list.get(xs, 1);
VEY
./veyra "$tmpdir/array-list-set.vey" | grep -q '^42$'

echo 'VEYRA tests: OK'
