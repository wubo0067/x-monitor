from time import sleep
from http.server import BaseHTTPRequestHandler, HTTPServer

class MyHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        content_length = self.headers.get('Content-Length')
        if content_length:
            content_length = int(content_length)
            print(f"Request data length: {content_length}")

        self.send_response(200)
        self.send_header('Content-type', 'text/plain')
        self.end_headers()
        self.wfile.write(b'hello')

httpd = HTTPServer(('localhost', 8000), MyHandler)
httpd.serve_forever()