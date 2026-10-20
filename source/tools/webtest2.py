import http.server, http.client, sys
SITE=sys.argv[1]
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(s,*a,**k): super().__init__(*a,directory=SITE,**k)
    def proxy(s):
        n=int(s.headers.get('Content-Length') or 0); body=s.rfile.read(n) if n else None
        c=http.client.HTTPConnection('127.0.0.1',18090)
        hd={k:v for k,v in s.headers.items()}; hd['X-Forwarded-For']='127.0.0.1'
        c.request(s.command,s.path,body=body,headers=hd); r=c.getresponse(); d=r.read()
        s.send_response(r.status)
        for k,v in r.getheaders():
            if k.lower() in ('content-type','set-cookie'): s.send_header(k,v.replace('; Secure',''))
        s.send_header('Content-Length',str(len(d))); s.end_headers(); s.wfile.write(d)
    def do_GET(s):
        if s.path.startswith('/api/'): s.proxy()
        else: super().do_GET()
    def do_POST(s): s.proxy()
    def log_message(s,*a): pass
http.server.ThreadingHTTPServer(("127.0.0.1",18080),H).serve_forever()
