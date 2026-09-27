#!/usr/bin/env python3
"""A tiny OpenAI-compatible Chat Completions server for testing the news
classifier without a real LLM or API key.

  python3 tools/mock_openai_server.py --port 8089
  bazel run //:twn_election -- --simulate --news --llm_base_url=http://127.0.0.1:8089/v1

It implements POST /v1/chat/completions and GET /v1/models. The "model" reads
the candidate list and article out of the prompt that twn_election sends and
answers with keyword rules, in the exact JSON shape a real model is asked
for. Use --latency to mimic a slow model, --fail-rate to test error handling.
"""
import argparse
import json
import random
import re
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

GOOD = ["上升", "肯定", "力挺", "熱烈", "看好", "支持者", "宣布勝選", "領先", "當選"]
BAD = ["下滑", "質疑", "批評", "不如預期", "吃緊", "跳票", "落後", "敗選"]


def classify(prompt):
    cands = re.findall(r"^- (\S+) \| (\S+) \| ([^|]*) \|", prompt, re.M)
    title = re.search(r"^Title: (.*)$", prompt, re.M)
    text = re.search(r"^Text: (.*)$", prompt, re.M)
    title = title.group(1) if title else ""
    body = (title + " " + (text.group(1) if text else ""))
    # Subject: the candidate named first in the title.
    ordered = sorted(((title.find(n) if n in title else 10**6, cid, n) for cid, n, _ in cands))
    subject = ordered[0][1] if ordered else None
    score = sum(w in body for w in GOOD) - sum(w in body for w in BAD)
    verdict = "good" if score > 0 else "bad" if score < 0 else "neutral"
    out = []
    for cid, name, _ in cands:
        if cid == subject:
            out.append({"candidate_id": cid, "sentiment": verdict,
                        "reason": "標題主角，關鍵詞判斷" if verdict != "neutral" else "例行活動"})
        else:
            out.append({"candidate_id": cid, "sentiment": "neutral", "reason": "僅被提及"})
    summary = title[:60]
    return {"summary": summary, "assessments": out}


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.rstrip("/").endswith("/models"):
            self._send(200, {"object": "list", "data": [{"id": "mock-sentiment-1", "object": "model"}]})
        else:
            self._send(404, {"error": {"message": "not found"}})

    def do_POST(self):
        if not self.path.rstrip("/").endswith("/chat/completions"):
            self._send(404, {"error": {"message": "not found"}})
            return
        if self.server.api_key and self.headers.get("Authorization") != "Bearer " + self.server.api_key:
            self._send(401, {"error": {"message": "invalid api key"}})
            return
        req = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        time.sleep(self.server.latency)
        if random.random() < self.server.fail_rate:
            self._send(500, {"error": {"message": "mock failure"}})
            return
        prompt = "\n".join(m.get("content", "") for m in req.get("messages", []) if m.get("role") == "user")
        answer = classify(prompt)
        self.server.requests += 1
        self._send(200, {
            "id": "chatcmpl-mock-%d" % self.server.requests,
            "object": "chat.completion",
            "created": int(time.time()),
            "model": req.get("model", "mock-sentiment-1"),
            "choices": [{"index": 0, "finish_reason": "stop",
                         "message": {"role": "assistant",
                                     "content": "```json\n" + json.dumps(answer, ensure_ascii=False) + "\n```"}}],
            "usage": {"prompt_tokens": len(prompt), "completion_tokens": 50, "total_tokens": len(prompt) + 50},
        })

    def log_message(self, fmt, *args):
        if self.server.verbose:
            super().log_message(fmt, *args)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8089)
    ap.add_argument("--api-key", default="", help="require this bearer token")
    ap.add_argument("--latency", type=float, default=0.2, help="seconds per request")
    ap.add_argument("--fail-rate", type=float, default=0.0)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    srv.api_key, srv.latency, srv.fail_rate, srv.verbose, srv.requests = (
        args.api_key, args.latency, args.fail_rate, args.verbose, 0)
    print(f"mock OpenAI server on http://127.0.0.1:{args.port}/v1", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
