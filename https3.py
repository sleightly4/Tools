from http.server import HTTPServer, BaseHTTPRequestHandler
import ssl
import os
# Variables 
ADDRESS = "127.0.0.1"
PORT = 82


class MyHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            # Extract the file path from the URL
            file_path = self.path[1:]

            # Check if the file exists
            if os.path.exists(file_path) and os.path.isfile(file_path):
                # If the file exists, send its content
                with open(file_path, 'rb') as file:
                    self.send_response(200)
                    self.send_header('Content-type', 'application/octet-stream')
                    self.send_header('Content-Disposition', 'attachment; filename="{}"'.format(os.path.basename(file_path)))
                    self.end_headers()
                    self.wfile.write(file.read())
            else:
                # If the file does not exist, return a 404 error
                self.send_response(404)
                self.send_header('Content-type', 'text/plain')
                self.end_headers()
                self.wfile.write(b'File not found')

        except Exception as e:
            # Handle exceptions and return a 500 error
            self.send_response(500)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(str(e).encode())

    def do_POST(self):
        try:
            # Get the length of the incoming content
            content_length = int(self.headers['Content-Length'])
            # Read the content (file data) from the request
            file_data = self.rfile.read(content_length)

            # Get the path where you want to save the uploaded file
            upload_path = os.path.join(os.getcwd(), 'uploads', 'uploaded_file.txt')

            # Save the file
            with open(upload_path, 'wb') as file:
                file.write(file_data)

            # Send a response indicating successful upload
            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(b'File uploaded successfully')

        except Exception as e:
            # Handle exceptions and return a 500 error
            self.send_response(500)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(str(e).encode())

httpd = HTTPServer((ADDRESS, PORT), MyHandler)

httpd.socket = ssl.wrap_socket(httpd.socket,
                               keyfile="./key.pem",
                               certfile="./cert.pem",
                               server_side=True)

print(f"Server running on {ADDRESS}:{PORT}")
httpd.serve_forever()
