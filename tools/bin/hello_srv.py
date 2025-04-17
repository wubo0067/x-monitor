#!/usr/bin/env python3.9

from time import sleep
import random
import string
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

class MyHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        content_length = self.headers.get('Content-Length')
        if content_length:
            content_length = int(content_length)
            print(f"Request data length: {content_length}")

        random_data = ''.join(random.choices(string.ascii_letters + string.digits, k=30720))
        response_data = random_data.encode('utf-8')

        self.send_response(200)
        self.send_header('Content-type', 'text/plain')
        self.send_header('Content-Length', len(response_data))
        self.end_headers()
        #sleep(6)
        self.wfile.write(response_data)

httpd = ThreadingHTTPServer(('0.0.0.0', 8000), MyHandler)
httpd.serve_forever()