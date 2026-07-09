#!/usr/bin/env python3
"""Convert test files from 'main -> int { return X }' to 'main proc() { os.exit(X) }'."""

import glob
import os
import re
import sys


def find_main_ctx(lines):
    """Find main proc and track brace depth. Returns (main_start, main_end) or None.
    Brace depth is 0 outside main, 1 inside main's body, 2+ inside nested procs.
    """
    main_start = None
    brace_depth = 0
    in_main = False

    for i, line in enumerate(lines):
        # Remove string literals and comments before counting braces
        # (simple approach: just count { and } literally)
        stripped = line.strip()

        if not in_main and re.match(r'main\s*::\s*proc', stripped):
            main_start = i
            in_main = True

        delta = stripped.count('{') - stripped.count('}')
        new_depth = brace_depth + delta

        if in_main and new_depth <= 0:
            # Main body closed
            return (main_start, i + 1, brace_depth)

        brace_depth = new_depth

    if in_main:
        return (main_start, len(lines), brace_depth)
    return None


def is_return_line(line):
    """Check if this line contains a 'return' statement (not in comment, not inside a word)."""
    stripped = line.strip()

    # Skip comment-only lines
    if stripped.startswith('//') or stripped.startswith('#'):
        return False

    # Find 'return' as a standalone word (not inside identifier like 'or_return')
    # Use negative lookbehind to ensure return is not part of a word
    # Match: start of line or non-word char before 'return', then 'return', then non-word char or end
    # We want to find 'return' that is used as a keyword, not inside a comment
    # Strategy: strip comments first, then check for \breturn\b

    # Simple: find return that's preceded by non-word char (or start of stripped content)
    # and followed by non-word char (or end of content)
    # Also skip lines where return is inside a // comment

    # Remove quoted strings and comments
    cleaned = re.sub(r'"[^"]*"', '', stripped)  # remove string literals
    cleaned = re.sub(r"'[^']*'", '', cleaned)    # remove char literals
    cleaned = re.sub(r'//.*', '', cleaned)       # remove line comments
    cleaned = re.sub(r'/\*.*?\*/', '', cleaned)  # remove block comments

    return bool(re.search(r'\breturn\b', cleaned))


def convert_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    original = content

    # Skip if main is already void
    if not re.search(r'main\s*::\s*proc\s*\(.*?\)\s*->\s*int', content):
        return False

    lines = content.split('\n')

    # Find main proc body range with brace depth tracking
    ctx = find_main_ctx(lines)
    if not ctx:
        print(f'  WARNING: Could not find main proc body in {filepath}')
        return False

    main_start, main_end, _ = ctx

    # 1. Add import "core:os" (only within first few lines, before main)
    if 'import "core:os"' not in content:
        insert_line = None
        for i in range(min(main_start, len(lines))):
            if re.match(r'^import\s+"[^"]*"\s*$', lines[i]):
                insert_line = i + 1
        if insert_line is None:
            for i in range(min(main_start, len(lines))):
                if re.match(r'^package\s+\w+', lines[i]):
                    insert_line = i + 1
                    break

        if insert_line is not None:
            lines.insert(insert_line, 'import "core:os"')
            # Adjust main range since we added a line
            if main_start >= insert_line:
                main_start += 1
                main_end += 1

    # 2. Replace main signature
    line_idx = main_start
    old_sig = lines[line_idx]
    new_sig = re.sub(r'(main\s*::\s*proc\s*\([^)]*\))\s*->\s*int\b', r'\1', old_sig)
    if new_sig != old_sig:
        lines[line_idx] = new_sig

    # 3. Replace return statements inside main body, tracking brace depth
    # Start with depth = 1 (main's body)
    # When we see {, depth++; when we see }, depth--
    # Only convert 'return' when depth == 1 (directly in main, not in nested proc)
    brace_depth = 1  # we're inside main's opening brace

    for i in range(main_start + 1, main_end):
        line = lines[i]
        stripped = line.strip()

        # Track brace depth before processing (so closing braces on return lines work)
        delta = stripped.count('{') - stripped.count('}')

        if not is_return_line(line):
            brace_depth += delta
            continue

        # We have a return statement - only convert at depth 1
        if brace_depth != 1:
            brace_depth += delta
            continue

        # Replace this return statement
        m = re.match(r'^(\s*)(.*)', line)
        indent = m.group(1) if m else ''
        rest = m.group(2) if m else ''

        # Remove anything from // onwards (comments)
        rest_no_comment = re.sub(r'//.*$', '', rest)

        # Find 'return' word
        ret_match = re.search(r'\breturn\b', rest_no_comment)
        if not ret_match:
            brace_depth += delta
            continue

        # Get everything before return
        before_ret = rest_no_comment[:ret_match.start()].rstrip()
        after_ret = rest_no_comment[ret_match.end():].strip()

        # Extract expression up to ; or } or end
        expr = ''
        closing = ''
        for ch in after_ret:
            if ch == ';':
                closing = ';'
                break
            elif ch == '}':
                closing = '}'
                break
            else:
                expr += ch

        expr = expr.strip()

        if not expr:
            brace_depth += delta
            continue

        # Build replacement
        # Preserve any prefix like "if cond { "
        if before_ret.endswith('{'):
            new_line = indent + before_ret + ' ' + 'os.exit(' + expr + ')'
            if closing == '}':
                new_line += ' }'
        elif '{' in before_ret:
            # Something like "if cond {" before return
            new_line = indent + before_ret + ' os.exit(' + expr + ')'
            if closing == '}':
                new_line += ' }'
        else:
            new_line = indent + 'os.exit(' + expr + ')'
            if closing == ';':
                new_line += ';'

        lines[i] = new_line
        brace_depth += delta

    new_content = '\n'.join(lines)
    if new_content != original:
        with open(filepath, 'w') as f:
            f.write(new_content)
        return True
    return False


def main():
    test_dir = sys.argv[1] if len(sys.argv) > 1 else 'tests'
    files = glob.glob(os.path.join(test_dir, '**/*.odin'), recursive=True)
    converted = 0
    skipped = 0
    for f in sorted(files):
        rel = os.path.relpath(f, test_dir)
        if rel.startswith('expected_to_fail') or rel.startswith('output'):
            continue
        if 'helper' in rel.split(os.sep):
            continue
        if rel == 'test_param.odin':
            continue

        try:
            if convert_file(f):
                print(f'  CONVERTED: {rel}')
                converted += 1
            else:
                skipped += 1
        except Exception as e:
            print(f'  ERROR: {rel}: {e}')
            import traceback
            traceback.print_exc()

    print(f'\nDone: {converted} converted, {skipped} skipped (already void)')


if __name__ == '__main__':
    main()
