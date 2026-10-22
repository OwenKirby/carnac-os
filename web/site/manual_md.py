# SPDX-License-Identifier: GPL-3.0-only
"""docs/manual.md -> the site's manual page body (make_site.py). Only the Markdown the manual uses: # headings,
paragraphs, - and 1. lists (one level of nesting), pipe tables, > quotes, **bold**, *italic*, `code` and [links](url).
The manual itself is a Claude Doc; docs/manual.md is its export, refreshed on each release."""
import html
import re

INLINE = [(re.compile(r"`([^`]+)`"), r"<code>\1</code>"),
          (re.compile(r"\*\*(.+?)\*\*"), r"<strong>\1</strong>"),
          (re.compile(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])"), r"<em>\1</em>")]
LINK = re.compile(r"\[([^\]]+)\]\((https?://[^)\s]+)\)")


def slug(text, seen):
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-") or "section"
    k, n = s, 2
    while k in seen:
        k, n = f"{s}-{n}", n + 1
    seen.add(k)
    return k


def inline(s):
    links = []
    s = LINK.sub(lambda m: links.append((m[1], m[2])) or f"\0{len(links) - 1}\0", s)
    s = html.escape(s.replace("\\|", "|").replace("\\_", "_").replace("\\*", "\x01"), quote=False)
    for rx, rep in INLINE:
        s = rx.sub(rep, s)
    s = re.sub(r"\0(\d+)\0", lambda m: '<a href="{1}">{0}</a>'.format(html.escape(links[int(m[1])][0], quote=False),
                                                                        html.escape(links[int(m[1])][1])), s)
    return s.replace("\x01", "*")


def cells(row):
    return [c.strip() for c in re.split(r"(?<!\\)\|", row.strip().strip("|"))]


def render(md):
    """-> (title, toc [(id, text)], body html)"""
    lines, out, toc, seen, title = md.splitlines(), [], [], set(), ""
    i = 0
    while i < len(lines):
        ln = lines[i]
        if not ln.strip():
            i += 1
            continue
        m = re.match(r"(#{1,4}) (.*)", ln)
        if m:
            lvl, text = len(m[1]), m[2].strip()
            if lvl == 1 and not title:
                title = text
            else:
                hid = slug(text, seen)
                if lvl == 2:
                    toc.append((hid, text))
                out.append(f'<h{lvl} id="{hid}">{inline(text)}</h{lvl}>')
            i += 1
        elif ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append(lines[i])
                i += 1
            head, body = cells(rows[0]), [cells(r) for r in rows[2:]]
            out.append('<div class="table"><table><thead><tr>' + "".join(f"<th>{inline(c)}</th>" for c in head) +
                       "</tr></thead><tbody>" + "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>"
                                                        for r in body) + "</tbody></table></div>")
        elif ln.startswith(">"):
            q = []
            while i < len(lines) and lines[i].startswith(">"):
                q.append(lines[i][1:].strip())
                i += 1
            out.append(f"<blockquote><p>{inline(' '.join(q))}</p></blockquote>")
        elif re.match(r"(- |\* |\d+\. )", ln):
            out.append(list_html(lines, i))
            while i < len(lines) and (re.match(r"\s*(- |\* |\d+\. )", lines[i]) or lines[i].startswith("  ")):
                i += 1
        else:
            p = []
            while i < len(lines) and lines[i].strip() and not re.match(r"(#|\||>|- |\* |\d+\. )", lines[i]):
                p.append(lines[i].strip())
                i += 1
            out.append(f"<p>{inline(' '.join(p))}</p>")
    return title, toc, "\n".join(out)


def list_html(lines, i):
    """the list starting at line i (its own items and an indented sub-list under any of them)"""
    ordered = bool(re.match(r"\d+\. ", lines[i]))
    items, sub = [], []

    def close_sub():
        if sub:
            items[-1] += list_html(sub, 0)
            sub.clear()
    while i < len(lines):
        ln = lines[i]
        m = re.match(r"(- |\* |\d+\. )(.*)", ln)
        if m:
            close_sub()
            items.append(inline(m[2]))
        elif re.match(r"\s{2,}(- |\* |\d+\. )", ln):
            sub.append(ln.lstrip())
        else:
            break
        i += 1
    close_sub()
    tag = "ol" if ordered else "ul"
    return f"<{tag}>" + "".join(f"<li>{x}</li>" for x in items) + f"</{tag}>"
