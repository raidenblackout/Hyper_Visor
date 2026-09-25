
"""
Zero-dependency paste-bin for shuttling HV logs between the host machine
(where you copied the framebuffer / net_log output) and this dev PC.

    python tools/log_paste.py           # bind 0.0.0.0:8787
    python tools/log_paste.py 9000      # custom port

Open http://<this-pc-ip>:8787/ in a browser on either side.

Three upload paths:
  * Small text: paste into the textarea, submit form.
  * Large text / file: click "choose file" and pick it. Browser reads it
    and streams the raw body to /paste (no form-encoding overhead, no
    multipart parsing headaches).
  * From a shell: curl --data-binary @logfile.txt http://host:8787/paste

Each entry is backed by a file on disk (under tools/log_paste_store/ next
to this script) so a multi-hundred-MiB paste doesn't sit in the server's
RAM forever. The HTML shows a preview (first N lines) + a "download" link
that streams the full file. Rendering 300k+ lines in one <pre> would kill
any browser; the preview keeps the page usable and download gives you the
whole thing on request.

Entries are ring-buffered to MAX_ENTRIES; oldest file is deleted when a
new entry evicts it. Restart the server to clear everything.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote
from datetime import datetime
from html import escape
from collections import deque
from pathlib import Path
import sys
import threading
import uuid

MAX_ENTRIES     = 100
MAX_BODY_BYTES  = 512 * 1024 * 1024
PREVIEW_LINES   = 400
PREVIEW_BYTES   = 512 * 1024

SCRIPT_DIR = Path(__file__).resolve().parent
STORE_DIR  = SCRIPT_DIR / "log_paste_store"
STORE_DIR.mkdir(exist_ok=True)

class Entry:
    __slots__ = ("id", "ts", "tag", "path", "size", "lines")

    def __init__(self, ts: str, tag: str, path: Path, size: int, lines: int):
        self.id    = path.stem
        self.ts    = ts
        self.tag   = tag
        self.path  = path
        self.size  = size
        self.lines = lines

_entries: "deque[Entry]" = deque(maxlen=MAX_ENTRIES)
_lock = threading.Lock()

def _evict_if_full():
    """deque(maxlen=...) drops the tail on append; we mirror that on disk
    by removing the file that's about to fall off. Called under _lock."""
    if len(_entries) >= MAX_ENTRIES:
        oldest = _entries[-1]
        try:
            oldest.path.unlink(missing_ok=True)
        except OSError:
            pass

def _store(body: bytes, tag: str) -> Entry:
    """Persist body to disk and return the created Entry. Never called
    while _lock is held (I/O first, then swap in under lock)."""
    ts   = datetime.now().strftime("%H:%M:%S")
    uid  = uuid.uuid4().hex[:12]
    path = STORE_DIR / f"{uid}.log"
    path.write_bytes(body)
    lines = body.count(b"\n") + (0 if body.endswith(b"\n") or not body else 1)
    entry = Entry(ts=ts, tag=tag, path=path, size=len(body), lines=lines)
    with _lock:
        _evict_if_full()
        _entries.appendleft(entry)
    return entry

def _preview(path: Path) -> str:
    """First PREVIEW_LINES lines, capped at PREVIEW_BYTES. If the file is
    tiny we return everything; large files get a tail marker."""
    try:
        with path.open("rb") as f:
            head = f.read(PREVIEW_BYTES)
    except OSError:
        return "(preview unavailable)"
    lines = head.splitlines(keepends=True)
    truncated = len(lines) > PREVIEW_LINES or f.tell() < path.stat().st_size
    text = b"".join(lines[:PREVIEW_LINES]).decode("utf-8", errors="replace")
    if truncated:
        text += "\n\n... (truncated -- use download for the full file)\n"
    return text

def _fmt_size(n: int) -> str:
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024:
            return f"{n:.1f} {unit}" if unit != "B" else f"{n} B"
        n /= 1024
    return f"{n:.1f} TiB"

PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>log_paste</title>
<style>
  :root {{ color-scheme: light dark; }}
  body {{ font-family: ui-monospace, Menlo, Consolas, monospace;
          max-width: 1200px; margin: 1.5rem auto; padding: 0 1rem; }}
  h1 {{ font-size: 1.1rem; margin: 0 0 0.6rem 0; }}
  form {{ display: flex; flex-direction: column; gap: 0.4rem; margin-bottom: 1.2rem; }}
  textarea {{ width: 100%; min-height: 7rem; font: inherit;
              padding: 0.5rem; box-sizing: border-box; }}
  .row {{ display: flex; gap: 0.5rem; align-items: center; flex-wrap: wrap; }}
  input[type="text"] {{ flex: 1; padding: 0.35rem; font: inherit; min-width: 8rem; }}
  input[type="file"] {{ font: inherit; }}
  button, a.btn {{ padding: 0.35rem 0.9rem; font: inherit; cursor: pointer;
                    text-decoration: none; color: inherit;
                    border: 1px solid rgba(127,127,127,.5); background: transparent;
                    border-radius: 3px; }}
  .entry {{ border: 1px solid rgba(127,127,127,.35); border-radius: 6px;
            padding: 0.5rem 0.75rem; margin-bottom: 0.75rem; }}
  .meta {{ display: flex; justify-content: space-between; opacity: .8;
           font-size: .85rem; margin-bottom: 0.35rem; gap: 0.5rem;
           flex-wrap: wrap; align-items: center; }}
  pre {{ white-space: pre; word-break: normal; margin: 0;
         max-height: 30rem; overflow: auto;
         background: rgba(127,127,127,.08); padding: 0.4rem; border-radius: 3px; }}
  .tag {{ display: inline-block; padding: 0 0.4rem; border-radius: 3px;
          background: rgba(127,127,127,.2); font-size: .8rem; }}
  .hint {{ opacity: .7; font-size: .85rem; margin: 0.4rem 0 0.8rem 0; }}
  progress {{ width: 100%; }}
</style></head><body>
<h1>log_paste  <span class="tag">{count}/{cap}</span></h1>
<form method="POST" action="/">
  <div class="row">
    <input type="text" name="tag" id="tag" placeholder="label (optional)" maxlength="40">
    <button type="submit">paste text</button>
    <label class="btn">choose file<input id="file" type="file" style="display:none"></label>
    <span id="upstat" class="hint"></span>
    <button type="submit" name="_clear" value="1" formnovalidate
            onclick="return confirm('clear all entries?');">clear all</button>
  </div>
  <textarea name="text" placeholder="paste log here, then click 'paste text' (small pastes only)"></textarea>
</form>
<p class="hint">curl:  curl --data-binary @file.log -H 'X-Tag:label' http://HOST:PORT/paste</p>
{entries}
<script>
document.getElementById('file').addEventListener('change', async (e) => {{
  const f = e.target.files[0];
  if (!f) return;
  const stat = document.getElementById('upstat');
  const tag  = document.getElementById('tag').value || f.name;
  stat.textContent = 'uploading ' + f.name + '...';
  try {{
    const r = await fetch('/paste?tag=' + encodeURIComponent(tag), {{
      method: 'POST',
      headers: {{ 'Content-Type': 'application/octet-stream' }},
      body: f,
    }});
    if (r.ok) {{ location.reload(); }}
    else     {{ stat.textContent = 'upload failed: ' + r.status + ' ' + await r.text(); }}
  }} catch (err) {{
    stat.textContent = 'upload error: ' + err;
  }}
}});
</script>
</body></html>
"""

ENTRY_TMPL = """<div class="entry">
  <div class="meta">
    <span>{ts}  {tag_span}<span class="tag">{lines} lines</span> <span class="tag">{size}</span></span>
    <span>
      <a class="btn" href="/download/{id}" download>download</a>
      <button onclick="navigator.clipboard.writeText(document.getElementById('p{id}').textContent)">copy preview</button>
      <a class="btn" href="/delete/{id}" onclick="return confirm('delete?');">delete</a>
    </span>
  </div>
  <pre id="p{id}">{preview}</pre>
</div>
"""

def render_page() -> bytes:
    with _lock:
        snapshot = list(_entries)
    parts = []
    for e in snapshot:
        tag_span = f'<span class="tag">{escape(e.tag)}</span> ' if e.tag else ""
        parts.append(ENTRY_TMPL.format(
            id=e.id, ts=escape(e.ts), tag_span=tag_span,
            lines=e.lines, size=_fmt_size(e.size),
            preview=escape(_preview(e.path)),
        ))
    page = PAGE.format(count=len(snapshot), cap=MAX_ENTRIES,
                       entries="".join(parts) or "<p>(empty)</p>")
    return page.encode("utf-8")

def _find(entry_id: str) -> Entry | None:

    if not entry_id.isalnum() or len(entry_id) > 32:
        return None
    with _lock:
        for e in _entries:
            if e.id == entry_id:
                return e
    return None

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            self._send(200, "text/html; charset=utf-8", render_page())
        elif path.startswith("/download/"):
            e = _find(path[len("/download/"):])
            if not e:
                self._send(404, "text/plain", b"not found\n"); return
            self._send_file(e)
        elif path.startswith("/delete/"):
            eid = path[len("/delete/"):]
            with _lock:
                for i, e in enumerate(_entries):
                    if e.id == eid:
                        try: e.path.unlink(missing_ok=True)
                        except OSError: pass
                        del _entries[i]
                        break
            self.send_response(303); self.send_header("Location", "/"); self.end_headers()
        else:
            self._send(404, "text/plain", b"not found\n")

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._send(400, "text/plain", b"bad length\n"); return
        if length <= 0 or length > MAX_BODY_BYTES:
            self._send(413, "text/plain",
                       f"payload too large (cap {_fmt_size(MAX_BODY_BYTES)})\n".encode()); return

        if path == "/paste":

            tag = self._query_param("tag") or self.headers.get("X-Tag", "")
            body = self._read_all(length)
            if body is None: return
            _store(body, tag.strip()[:40])
            self._send(200, "text/plain", b"ok\n")
            return

        if path == "/":
            body_bytes = self._read_all(length)
            if body_bytes is None: return
            form = parse_qs(body_bytes.decode("utf-8", errors="replace"),
                            keep_blank_values=True)
            if form.get("_clear", [""])[0]:
                with _lock:
                    for e in list(_entries):
                        try: e.path.unlink(missing_ok=True)
                        except OSError: pass
                    _entries.clear()
            else:
                text = form.get("text", [""])[0]
                tag  = form.get("tag", [""])[0].strip()[:40]
                if text.strip():
                    _store(text.encode("utf-8"), tag)
            self.send_response(303); self.send_header("Location", "/"); self.end_headers()
            return

        self._send(404, "text/plain", b"not found\n")

    def _query_param(self, key: str) -> str:
        _, _, q = self.path.partition("?")
        for kv in q.split("&"):
            if kv.startswith(key + "="):
                from urllib.parse import unquote
                return unquote(kv[len(key) + 1:])
        return ""

    def _read_all(self, length: int) -> bytes | None:

        chunks, remaining = [], length
        while remaining > 0:
            chunk = self.rfile.read(min(remaining, 1 << 20))
            if not chunk:
                self._send(400, "text/plain", b"short body\n"); return None
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def _send(self, status, ctype, body):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, e: Entry):
        try:
            size = e.path.stat().st_size
            f = e.path.open("rb")
        except OSError:
            self._send(404, "text/plain", b"file gone\n"); return
        try:
            fn = (e.tag or "log") + ".log"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(size))
            self.send_header("Content-Disposition",
                             f'attachment; filename="{quote(fn)}"')
            self.end_headers()
            while True:
                buf = f.read(1 << 20)
                if not buf: break
                self.wfile.write(buf)
        finally:
            f.close()

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

def main():
    port = 8787
    if len(sys.argv) > 1:
        port = int(sys.argv[1])

    for f in STORE_DIR.iterdir():
        if f.is_file():
            try: f.unlink()
            except OSError: pass
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"log_paste: http://0.0.0.0:{port}/  store={STORE_DIR}"
          f"  cap/paste={_fmt_size(MAX_BODY_BYTES)}", file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nbye", file=sys.stderr)

if __name__ == "__main__":
    main()
