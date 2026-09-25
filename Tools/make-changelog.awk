# WaveLathe - Copyright (C) 2026 Mark Van de Velde
# SPDX-License-Identifier: AGPL-3.0-or-later

# Builds CHANGELOG.md out of the version history in Source/Version.h, so that
# history stays written in exactly one place. Run from the project root:
#
#     awk -f Tools/make-changelog.awk Source/Version.h > CHANGELOG.md
#
# The history is a block comment, and a block comment has no way to say "new
# paragraph". Two things are used to find the breaks:
#
#   A bare "//" line is an explicit break. Use it in new entries.
#
#   Otherwise the wrapping is read. These comments are hard-wrapped, so a line
#   that stops early stopped because the author meant it to: if the next line's
#   first word would still have fitted, the break was deliberate. The wrap
#   column is measured per entry rather than once for the file, because it is
#   not the same throughout.
#
# That second rule recovers most breaks but cannot recover all of them. Where a
# paragraph happens to end on a line that fills the column, nothing
# distinguishes it from a wrap, and the two paragraphs run together. It reads
# densely; it never loses or reorders a word. Put a bare "//" line in when you
# write the entry and the guessing does not arise.

function firstword(s,   a) { split(s, a, " "); return a[1] }

/namespace version/ { stop = 1 }
stop { next }

{
    if ($0 ~ /^\/\/   [0-9]+\.[0-9]+\.[0-9]+ /) {
        rest = substr($0, 6)
        sp = index(rest, " ")
        v = substr(rest, 1, sp - 1)
        t = rest
        sub(/^[0-9.]+ +/, "", t)
        e++
        n++; ver[n] = v; text[n] = t; head[n] = 1; ent[n] = e; brk[n] = 0
        if (length(t) > emax[e]) emax[e] = length(t)
    }
    else if ($0 ~ /^\/\/ *$/ && n > 0 && e > 0) {
        brk[n] = 1          # an explicit paragraph break after the line before
    }
    else if ($0 ~ /^\/\/          /) {
        if (n > 0) {
            t = substr($0, 13)
            n++; ver[n] = ""; text[n] = t; head[n] = 0; ent[n] = e; brk[n] = 0
            if (length(t) > emax[e]) emax[e] = length(t)
        }
    }
}

END {
    print "# WaveLathe version history"
    print ""
    print "Every release, newest first. Written as each change was made."
    print ""

    for (i = 1; i <= n; i++) {
        if (head[i]) {
            if (i > 1 && para != "") { print para; print "" }
            para = ""
            print "## " ver[i]
            print ""
        }

        para = (para == "") ? text[i] : para " " text[i]

        if (i == n || head[i + 1])
            continue

        if (brk[i] || length(text[i]) + 1 + length(firstword(text[i + 1])) < emax[ent[i]]) {
            print para
            print ""
            para = ""
        }
    }

    if (para != "") print para
}
