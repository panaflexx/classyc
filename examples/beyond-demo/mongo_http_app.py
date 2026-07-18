"""mongo_http_app.py — FastAPI/uvicorn/motor HTTP wrapper for MongoDB.

Mirrors ClassyDB's REST API exactly (same routes, same response shapes) so
stress_client.py can drive both servers unchanged.  Bodies are parsed from
the raw request bytes (ClassyDB is Content-Type agnostic; stress_client.py
sends no Content-Type header):

    POST   /api/users            insert (server assigns doc-N id) -> 201 {_id}
    GET    /api/users            list all docs                    -> 200 [doc]
    GET    /api/users/{id}       get one                          -> 200 doc | 404
    POST   /api/users/query      mongo-style filter               -> 200 [doc]
    POST   /api/users/count      mongo-style filter               -> 200 {"count":N}
    POST   /api/users/index      {"field": name}                  -> 200 {}
    PUT    /api/users/{id}       mongo-style update ($set/$inc..) -> 200 {}
    DELETE /api/users/{id}                                         -> 204 | 404

Run:  .venv/bin/uvicorn mongo_http_app:app --host 127.0.0.1 --port 8000
"""

import json

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, Response
from motor.motor_asyncio import AsyncIOMotorClient
from pymongo import ReturnDocument

app = FastAPI()
client = AsyncIOMotorClient("mongodb://127.0.0.1:27017")
db = client.httpbench
users = db.users
counters = db.counters


async def body_json(request: Request):
    raw = await request.body()
    if not raw:
        return None
    try:
        return json.loads(raw)
    except ValueError:
        return None


@app.post("/api/users", status_code=201)
async def insert(request: Request):
    doc = await body_json(request)
    if not isinstance(doc, dict):
        return JSONResponse({"error": "expected JSON body"}, status_code=400)
    # App-side doc-N id, mirroring ClassyDB's NewDocId (atomic counter doc).
    c = await counters.find_one_and_update(
        {"_id": "users"}, {"$inc": {"seq": 1}},
        upsert=True, return_document=ReturnDocument.AFTER)
    new_id = f"doc-{c['seq']}"
    doc["_id"] = new_id
    await users.insert_one(doc)
    return {"_id": new_id}


@app.get("/api/users")
async def list_all():
    return await users.find().to_list(None)


@app.get("/api/users/{doc_id}")
async def get_one(doc_id: str):
    d = await users.find_one({"_id": doc_id})
    if d is None:
        return JSONResponse({"error": f"{doc_id} not found"}, status_code=404)
    return d


@app.post("/api/users/query")
async def query(request: Request):
    filt = await body_json(request)
    if not isinstance(filt, dict):
        filt = {}
    return await users.find(filt).to_list(None)


@app.post("/api/users/count")
async def count(request: Request):
    filt = await body_json(request)
    if not isinstance(filt, dict):
        filt = {}
    return {"count": await users.count_documents(filt)}


@app.post("/api/users/index")
async def create_index(request: Request):
    body = await body_json(request)
    field = body.get("field", "") if isinstance(body, dict) else ""
    if not field:
        return JSONResponse({"error": "expected 'field'"}, status_code=400)
    await users.create_index(field)
    return {}


@app.put("/api/users/{doc_id}")
async def update(doc_id: str, request: Request):
    upd = await body_json(request)
    if not isinstance(upd, dict):
        return JSONResponse({"error": "expected JSON body"}, status_code=400)
    await users.update_one({"_id": doc_id}, upd)
    return {}


@app.delete("/api/users/{doc_id}")
async def delete(doc_id: str):
    r = await users.delete_one({"_id": doc_id})
    if r.deleted_count == 0:
        return JSONResponse({"error": f"{doc_id} not found"}, status_code=404)
    return Response(status_code=204)
