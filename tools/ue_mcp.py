"""Command-line client for the editor's native, local MCP server.

Run with .venv-mcp/Scripts/python.exe after tools/unreal.ps1 -Action Editor.
Discover schemas with `list`; call a discovered tool with `call NAME --args-file`.
Tool calls are serialized, as required by Unreal's game-thread dispatcher.
"""
import argparse
import asyncio
import json
from pathlib import Path
from datetime import timedelta
from urllib.parse import urlparse

import httpx
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client


async def run(args):
    if urlparse(args.url).hostname not in ("localhost", "127.0.0.1", "::1"):
        raise ValueError("This project uses the local Unreal editor only.")
    async with httpx.AsyncClient(timeout=60, trust_env=False) as http:
        async with streamable_http_client(args.url, http_client=http) as (read, write, _):
            async with ClientSession(read, write, read_timeout_seconds=timedelta(seconds=60)) as session:
                info = await session.initialize()
                if args.action == "list":
                    result = (await session.list_tools()).model_dump(mode="json", exclude_none=True)
                    output = {"server": info.serverInfo.model_dump(mode="json"), **result}
                else:
                    params = json.loads(Path(args.args_file).read_text(encoding="utf-8-sig")) if args.args_file else {}
                    result = await session.call_tool(args.name, arguments=params)
                    output = result.model_dump(mode="json", exclude_none=True)
                    if result.isError:
                        raise RuntimeError(json.dumps(output, ensure_ascii=False))
                text = json.dumps(output, ensure_ascii=False, indent=2)
                if args.output:
                    Path(args.output).write_text(text + "\n", encoding="utf-8")
                    print(json.dumps({"output": str(Path(args.output).resolve()), "bytes": len((text + "\n").encode("utf-8"))}, ensure_ascii=False))
                else:
                    print(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8008/mcp")
    parser.add_argument("--output")
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("list")
    call = sub.add_parser("call")
    call.add_argument("name")
    call.add_argument("--args-file")
    args = parser.parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
