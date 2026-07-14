from fastapi import APIRouter


router = APIRouter()


@router.post("/command")
async def command(endpoint, command):
    if command == "status":
        await endpoint.send_json({"ok": True})


def command_object(cmd):
    if cmd.command in ("start", "stop"):
        return cmd.command
