"""Theme API router."""

from __future__ import annotations

from fastapi import APIRouter
from fastapi.responses import JSONResponse, Response

from engines.theme_service import list_themes, read_theme_file

router = APIRouter(tags=["themes"])


@router.get("/api/themes")
async def api_themes():
    return {"themes": list_themes()}


@router.get("/api/themes/{theme_id}/{filename}")
async def api_theme_file(theme_id: str, filename: str):
    try:
        data, mime = read_theme_file(theme_id, filename)
    except FileNotFoundError:
        return JSONResponse(status_code=404, content={"error": "not found"})
    return Response(content=data, media_type=mime)
