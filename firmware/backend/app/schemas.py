from pydantic import BaseModel, Field


class UserCreate(BaseModel):
    name: str = Field(min_length=1, max_length=32)


class BetCreate(BaseModel):
    user_id: int
    kind: str
    choice: str
    amount: int = Field(gt=0, le=100000)


class PollCreate(BaseModel):
    kind: str
    title: str
    options: list[str] = Field(min_length=2, max_length=8)


class VoteCreate(BaseModel):
    user_id: int
    choice: str


class CommandRequest(BaseModel):
    type: str
    payload: dict = Field(default_factory=dict)


class RunStart(BaseModel):
    algorithm: str = Field(default="DFS", pattern="^(DFS|BFS)$")
    speed_limit: int = Field(default=100, ge=1, le=100)


class RunFinish(BaseModel):
    reached_finish: bool = True
    elapsed_ms: int = Field(default=0, ge=0)
    obstacle_count: int = Field(default=0, ge=0)
    crossing_count: int = Field(default=0, ge=0)

