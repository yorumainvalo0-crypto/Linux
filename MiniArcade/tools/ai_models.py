#!/usr/bin/env python3
"""Keeps MiniArcade/ai-models.txt in line with the free models on OpenRouter.

Ranked models that are no longer offered for free are dropped, new free
chat models are added at the end (the order above them is kept - it is
the ranking). Comment lines stay as they are. Usage:
    ai_models.py [models.json]     (default: download the list)
"""
import json, os, sys, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
LIST = os.path.join(HERE, "..", "ai-models.txt")
SKIP = ("content-safety", "guard", "embed", "rerank")       # not chat models

def free_models(data):
    out = []
    for m in data["data"]:
        mid = m["id"]
        p = m.get("pricing", {})
        mods = m.get("architecture", {}).get("output_modalities") or ["text"]
        if not mid.endswith(":free") or any(s in mid for s in SKIP) or "text" not in mods:
            continue
        if any(float(p.get(k, 0) or 0) != 0 for k in ("prompt", "completion", "request")):
            continue
        out.append(mid)
    return out

def main():
    if len(sys.argv) > 1:
        data = json.load(open(sys.argv[1]))
    else:
        with urllib.request.urlopen("https://openrouter.ai/api/v1/models", timeout=60) as r:
            data = json.load(r)
    free = free_models(data)
    if len(free) < 3:
        sys.exit("only %d free models found - list left as it is" % len(free))
    lines = open(LIST).read().splitlines()
    head = [l for l in lines if l.startswith("#") or not l.strip()]
    ranked = [l.strip() for l in lines if l.strip() and not l.startswith("#")]
    keep = [m for m in ranked if m in free]
    new = [m for m in free if m not in keep]
    out = "\n".join(head + keep + new) + "\n"
    if out != open(LIST).read():
        open(LIST, "w").write(out)
        print("dropped:", [m for m in ranked if m not in free])
        print("added:", new)
    else:
        print("no change")

if __name__ == "__main__":
    main()
