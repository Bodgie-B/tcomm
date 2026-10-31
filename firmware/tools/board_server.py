# Serves the Pd board pages and saves the display list a page POSTs to /save/<name> as
# <name>.json, for tools/boards_to_art.py. Run from the firmware folder:
#   python tools/board_server.py docs/design/boards <folder for the json>
# then open http://127.0.0.1:8766/<Board>.html at 800 x 480 and run tools/board_extract.js in it.
import http.server, os, sys

ROOT = sys.argv[1]   # folder with the board pages
OUT = sys.argv[2]    # folder for the saved json
os.makedirs(OUT, exist_ok=True)


class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=ROOT, **k)

    def do_POST(self):
        if not self.path.startswith('/save/'):
            self.send_error(404)
            return
        name = os.path.basename(self.path[6:]) or 'out'
        n = int(self.headers.get('Content-Length', 0))
        with open(os.path.join(OUT, name + '.json'), 'wb') as f:
            f.write(self.rfile.read(n))
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'saved')


http.server.ThreadingHTTPServer(('127.0.0.1', 8766), H).serve_forever()
