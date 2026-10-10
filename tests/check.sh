#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

./veyra --version | grep -q 'VEYRA 0.2.0'
./veyra --builtins | grep -q 'text.len'
./veyra --check examples/hello.vey | grep -q 'синтаксис OK'
./veyra examples/hello.vey | grep -q 'Привет! Это язык VEYRA.'
./veyra examples/basics.vey | grep -q 'Квадрат 7 = 49'
./veyra examples/basics.vey | grep -q 'До старта: 1'
./veyra examples/security_audit.vey | grep -q 'SHA-256 /etc/passwd:'
./veyra examples/advanced.vey | grep -q 'Сумма = 13'
./veyra examples/advanced.vey | grep -q 'VEYRA LINUX'
./veyra examples/factorial.vey | grep -q '5! = 120'

cat > "$tmpdir/loop.vey" <<'VEY'
let i = 0;
while (true) { i = i + 1; }
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

cat > "$tmpdir/hash-link.vey" <<VEY
print security.sha256("$tmpdir/passwd-link");
VEY
ln -s /etc/passwd "$tmpdir/passwd-link"
if ./veyra "$tmpdir/hash-link.vey" >/dev/null 2>&1; then
  echo 'FAIL: hashing a symbolic link was not blocked' >&2; exit 1
fi

cat > "$tmpdir/constant.vey" <<'VEY'
const fixed = 10;
fixed = 11;
VEY
if ./veyra "$tmpdir/constant.vey" > /dev/null 2>&1; then
  echo 'FAIL: const value was mutable' >&2; exit 1
fi

cat > "$tmpdir/break-outside.vey" <<'VEY'
break;
VEY
if ./veyra "$tmpdir/break-outside.vey" > /dev/null 2>&1; then
  echo 'FAIL: break outside loop was accepted' >&2; exit 1
fi

cat > "$tmpdir/scope.vey" <<'VEY'
if (true) { let local_value = 1; }
print local_value;
VEY
if ./veyra "$tmpdir/scope.vey" > /dev/null 2>&1; then
  echo 'FAIL: block-local variable leaked outside scope' >&2; exit 1
fi


# Deeply nested expressions must fail gracefully, not overflow the C++ stack.
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

echo 'VEYRA tests: OK'
