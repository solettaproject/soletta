{{
import os

def gen_headers(headers):
    headers = os.getenv(headers)
    headers = headers.strip()
    headers = headers.split(" ")
    for h in headers:
    	st.println("#include \"%s\"" % (h.split("/")[-1]))
}}
