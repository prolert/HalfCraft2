"""checks the lists in source\\src\\core\\hc_campaign_models.h against the installed games. half-life 2, episode one and
episode two play as one game, the later campaign's content mounted over the earlier's; server.dll
(source\\src\\server\\hc_campaign_models.cpp) copies the characters a map's campaign plays from that campaign's own
content into the game folder. this plans each set (half-life 2, episode one, lost coast; episode two's is the merged
mount) the way server.dll does, from the paks each engine's gameinfo.txt mounts, and checks:

- that with the copies no model anywhere plays fewer sequences than under valve's own mount of the campaign (a
  character missing from CAMPAIGN_CHARACTERS otherwise);
- that every model a set copies is in CAMPAIGN_MODELS_SWAPPED, and every one there is copied by some set;
- that CAMPAIGN_MODEL_DEPENDENTS are exactly the models that include a swapped one and aren't swapped themselves;
- that each copied model's files go together (checksums, its anim block) and none of another pak slips in;
- that gameinfo.txt's merged order of the campaigns' own path ids is MERGED_MODEL_SOURCES', and that GAME's version of
  each model is the one server.dll takes for the merged one.

python tools/game/campaign_models.py

prints each set's plan and the checksums its maps' characters should have (hc_campaign_models.cpp logs them as a map
loads), and exits 1 when the lists need changing, saying what. run it after valve updates the games. needs half-life 2
installed; half-life 2: deathmatch's mount is checked when that's installed too.
"""

import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import REPO, ToolError, engines

HEADER = REPO / "source/src/core/hc_campaign_models.h"
VPK_SIGNATURE = 0x55AA1234
VPK_IN_DIRECTORY = 0x7FFF  # a file kept in the _dir.vpk itself
# what server.dll's model_set_sources() gives each set: its own content's path ids, first to last
SETS = {
    "half-life 2": ("hc_hl2",),
    "episode one": ("hc_ep1", "hc_hl2"),
    "lost coast": ("hc_lc", "hc_hl2"),
}
# studiohdr_t on disk
MIN_HEADER = 408
LOCAL_SEQUENCES, INCLUDES, ANIM_BLOCK_NAME = 188, 336, 348
SEQUENCE_BYTES = 212
SEARCH_PATH = re.compile(r"^\s*([A-Za-z0-9_+]+)\s+(\S+)\s*(?://.*)?$")


def c_string(data: bytes, at: int) -> str:
    return data[at : data.index(b"\0", at)].decode("latin-1")


def normal(name: str) -> str:
    return name.replace("\\", "/").lower()


def short(name: str) -> str:
    return name.removeprefix("models/").removesuffix(".mdl")


class Pak:
    """a vpk (version 1 or 2): its directory, and its files read from the numbered archives."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.label = path.as_posix()
        self.data = path.read_bytes()
        signature, version, tree = struct.unpack_from("<III", self.data, 0)
        if signature != VPK_SIGNATURE:
            raise ToolError(f"{path} isn't a vpk")
        header = 12 if version == 1 else 28
        self.tree_end = header + tree
        self.entries: dict[str, tuple[int, int, int, bytes]] = {}
        self.archives: dict[int, BinaryIO] = {}
        at = header
        while extension := self._string(at):
            at += len(extension) + 1
            while folder := self._string(at):
                at += len(folder) + 1
                while name := self._string(at):
                    at += len(name) + 1
                    _, preload, archive, offset, length, _ = struct.unpack_from("<IHHIIH", self.data, at)
                    at += 18
                    path_name = f"{folder.strip()}/{name}.{extension}" if folder.strip() else f"{name}.{extension}"
                    self.entries[normal(path_name)] = (archive, offset, length, self.data[at : at + preload])
                    at += preload
                at += 1
            at += 1

    def _string(self, at: int) -> str:
        return c_string(self.data, at)

    def has(self, name: str) -> bool:
        return name in self.entries

    def names(self) -> list[str]:
        return list(self.entries)

    def read(self, name: str) -> bytes:
        archive, offset, length, preload = self.entries[name]
        if archive == VPK_IN_DIRECTORY:
            return preload + self.data[self.tree_end + offset : self.tree_end + offset + length]
        if archive not in self.archives:
            self.archives[archive] = self.path.with_name(self.path.name.replace("_dir.vpk", f"_{archive:03d}.vpk")).open("rb")
        handle = self.archives[archive]
        handle.seek(offset)
        return preload + handle.read(length)


class Folder:
    """loose files under a folder."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.label = path.as_posix()

    def has(self, name: str) -> bool:
        return (self.path / name).is_file()

    def names(self) -> list[str]:
        models = self.path / "models"
        return [normal(found.relative_to(self.path).as_posix()) for found in models.rglob("*.mdl")] if models.is_dir() else []

    def read(self, name: str) -> bytes:
        return (self.path / name).read_bytes()


Source = Pak | Folder
OPENED: dict[Path, Source] = {}


@dataclass(frozen=True)
class Model:
    """what a .mdl's header says that matters here."""

    checksum: int
    includes: tuple[str, ...]
    anim_block: str  # empty: none


MODELS: dict[tuple[str, str], Model | None] = {}


def model_of(source: Source, name: str) -> Model | None:
    """a .mdl's header (read once a run); None when it isn't a studio model (valve's install has empty loose ones)."""
    key = (source.label, name)
    if key not in MODELS:
        data = source.read(name)
        if len(data) < MIN_HEADER or data[:4] != b"IDST":
            MODELS[key] = None
            return None
        count, index = struct.unpack_from("<ii", data, INCLUDES)
        includes = tuple(normal(c_string(data, index + 8 * i + struct.unpack_from("<i", data, index + 8 * i + 4)[0])) for i in range(count))
        name_index, blocks = struct.unpack_from("<ii", data, ANIM_BLOCK_NAME)
        MODELS[key] = Model(struct.unpack_from("<I", data, 8)[0], includes, normal(c_string(data, name_index)) if blocks else "")
    return MODELS[key]


def sequence_labels(data: bytes) -> set[str]:
    count, index = struct.unpack_from("<ii", data, LOCAL_SEQUENCES)
    labels = set()
    for i in range(count):
        base = index + SEQUENCE_BYTES * i
        labels.add(c_string(data, base + struct.unpack_from("<i", data, base + 4)[0]).lower())
    return labels


def first(sources: list[Source], name: str) -> Source | None:
    return next((source for source in sources if source.has(name)), None)


@dataclass
class Lists:
    """hc_campaign_models.h's lists."""

    characters: list[str]
    swapped: list[str]
    dependents: list[str]
    companions: list[tuple[str, int]]
    merged: list[str]


def read_lists() -> Lists:
    text = HEADER.read_text(encoding="utf-8")

    def array(name: str) -> str:
        found = re.search(rf"\b{name}\[\]\s*=\s*\{{(.*?)\}};", text, re.DOTALL)
        if not found:
            raise ToolError(f"{name} not found in {HEADER}")
        return found[1]

    def strings(name: str) -> list[str]:
        return re.findall(r'"([^"]+)"', array(name))

    companions = [(extension, int(offset)) for extension, offset in re.findall(r'\{\s*"([^"]+)",\s*(\d+)\s*\}', array("MODEL_COMPANIONS"))]
    return Lists(
        strings("CAMPAIGN_CHARACTERS"), strings("CAMPAIGN_MODELS_SWAPPED"), strings("CAMPAIGN_MODEL_DEPENDENTS"), companions, strings("MERGED_MODEL_SOURCES")
    )


@dataclass
class Mount:
    """an engine's search paths for game content: GAME in order (without the game folder and custom\\*, where the copies
    and a player's own content go) and the campaigns' own path ids."""

    engine: str
    game: list[Source] = field(default_factory=list)
    ids: dict[str, list[Source]] = field(default_factory=dict)

    def chain(self, path_ids: tuple[str, ...]) -> list[Source]:
        return [source for path_id in path_ids for source in self.ids.get(path_id, [])]


def resolve_path(engine: str, written: str, folders: dict[str, Path]) -> Path:
    path = written
    for token, folder in folders.items():
        path = path.replace(token, f"{folder.as_posix()}/")
    if "|" in path:
        raise ToolError(f"{engine}'s gameinfo.txt: can't resolve {written}")
    resolved = Path(path)
    if resolved.suffix == ".vpk" and not resolved.stem.endswith("_dir"):
        resolved = resolved.with_name(f"{resolved.stem}_dir.vpk")
    return resolved


def mount_of(engine: engines.Engine, folders: dict[str, Path]) -> Mount:
    """the mount source\\mod\\<engine>\\gameinfo.txt describes, with its paks opened."""
    text = (REPO / "source/mod" / engine.name / "gameinfo.txt").read_text(encoding="utf-8")
    mount = Mount(engine.name)
    for line in text[text.index("SearchPaths") :].splitlines():
        found = SEARCH_PATH.match(line)
        if not found or "|gameinfo_path|" in found[2]:
            continue
        ids = [part.lower() for part in found[1].split("+")]
        if "game" not in ids and not any(path_id.startswith("hc_") for path_id in ids):
            continue
        path = resolve_path(engine.name, found[2], folders)
        if not path.exists():
            # the engine skips what isn't installed (another language's voices) as well; a campaign's own content is needed
            if any(path_id.startswith("hc_") for path_id in ids):
                raise ToolError(f"{engine.name}'s gameinfo.txt mounts {found[2]} for {found[1]}, which isn't installed ({path})")
            print(f"{engine.name}: {found[2]} isn't installed; the engine skips it too")
            continue
        if path not in OPENED:
            OPENED[path] = Pak(path) if path.suffix == ".vpk" else Folder(path)
        if "game" in ids:
            mount.game.append(OPENED[path])
        for path_id in ids:
            if path_id.startswith("hc_"):
                mount.ids.setdefault(path_id, []).append(OPENED[path])
    return mount


@dataclass
class Own:
    """a model as a set's own content has it, and what copying it takes."""

    path_id: str
    source: Source
    model: Model
    copied: bool = False
    files: list[str] = field(default_factory=list)
    error: str = ""


@dataclass
class Plan:
    models: dict[str, Own] = field(default_factory=dict)  # the ones copied
    skipped: list[str] = field(default_factory=list)

    def files(self) -> dict[str, Source]:
        return {name: own.source for own in self.models.values() for name in own.files}


class Planner:
    """a set's plan, as server.dll's plan_model_set() makes it."""

    def __init__(self, mount: Mount, lists: Lists, path_ids: tuple[str, ...]) -> None:
        self.mount = mount
        self.lists = lists
        self.path_ids = path_ids
        self.own: dict[str, Own | str] = {}  # a model's own version, or why there's none

    def first_of(self, path_ids: list[str] | tuple[str, ...], name: str) -> tuple[str, Source] | None:
        for path_id in path_ids:
            if source := first(self.mount.ids.get(path_id, []), name):
                return path_id, source
        return None

    def own_model(self, name: str) -> Own | str:
        if name not in self.own:
            found = self.first_of(self.path_ids, name)
            if not found:
                self.own[name] = f"{name} isn't in the campaign's own content"
                return self.own[name]
            model = model_of(found[1], name)
            if not model:
                self.own[name] = f"{name} in {found[1].label} isn't a studio model"
                return self.own[name]
            own = Own(found[0], found[1], model)
            winner = self.first_of(self.lists.merged, name)
            theirs = model_of(winner[1], name) if winner else None
            own.copied = bool(winner) and winner[0] != own.path_id and (not theirs or theirs.checksum != model.checksum)
            if own.copied:
                self.add_files(name, own)
            self.own[name] = own
        return self.own[name]

    def add_files(self, name: str, own: Own) -> None:
        if name not in self.lists.swapped:
            own.error = f"{name} would be copied but isn't in CAMPAIGN_MODELS_SWAPPED"
            return
        own.files.append(name)
        ahead = self.lists.merged[: self.lists.merged.index(own.path_id)] if own.path_id in self.lists.merged else []
        for extension, offset in self.lists.companions:
            companion = name.removesuffix(".mdl") + extension
            if not own.source.has(companion):
                if self.first_of(ahead, companion):
                    own.error = f"{companion} isn't in {own.source.label} but a merged source ahead of it has one"
                continue
            checksum = struct.unpack_from("<I", own.source.read(companion), offset)[0]
            if checksum != own.model.checksum:
                own.error = f"{companion}'s checksum {checksum:08x} isn't the .mdl's {own.model.checksum:08x}"
            own.files.append(companion)
        if own.model.anim_block:
            if not own.source.has(own.model.anim_block):
                own.error = f"{own.model.anim_block} isn't in {own.source.label}"
            own.files.append(own.model.anim_block)

    def plan(self) -> Plan:
        plan = Plan()
        kept: set[str] = set()
        for root in self.lists.characters:
            if not self.first_of(self.path_ids, root):
                continue
            closure, failure, i = [root], "", 0
            while i < len(closure) and not failure:
                own = self.own_model(closure[i])
                failure = own if isinstance(own, str) else own.error
                if isinstance(own, Own):
                    closure += [include for include in own.model.includes if include not in closure]
                i += 1
            if failure:
                plan.skipped.append(f"{root}'s closure: {failure}")
            else:
                kept.update(closure)
        for name in sorted(kept):
            own = self.own[name]
            if isinstance(own, Own) and own.copied:
                plan.models[name] = own
        return plan


class Resolver:
    """closures and sequences of models under some sources, with some files laid over them."""

    def __init__(self, sources: list[Source], overlay: dict[str, Source] | None = None) -> None:
        self.sources = sources
        self.overlay = overlay or {}

    def find(self, name: str) -> Source | None:
        return self.overlay.get(name) or first(self.sources, name)

    def closure(self, name: str) -> list[tuple[str, int]]:
        members, out, i = [name], [], 0
        while i < len(members):
            source = self.find(members[i])
            model = model_of(source, members[i]) if source else None
            out.append((members[i], model.checksum if model else -1))
            members += [include for include in (model.includes if model else ()) if include not in members]
            i += 1
        return out

    def sequences(self, name: str) -> set[str]:
        labels: set[str] = set()
        for member, checksum in self.closure(name):
            if checksum != -1:
                labels |= sequence_labels(self.find(member).read(member))
        return labels


def all_models(mount: Mount) -> dict[str, list[Source]]:
    """every .mdl any mounted source has, with the sources that have it."""
    found: dict[str, list[Source]] = {}
    sources = {source.label: source for source in [*mount.game, *mount.chain(tuple(mount.ids))]}
    for source in sources.values():
        for name in source.names():
            if name.startswith("models/") and name.endswith(".mdl"):
                found.setdefault(name, []).append(source)
    return found


def lost_sequences(mount: Mount, path_ids: tuple[str, ...], plan: Plan, models: dict[str, list[Source]]) -> list[str]:
    """the models that, with the plan's copies over GAME, still play fewer sequences than under the set's own content."""
    own, effective = Resolver(mount.chain(path_ids)), Resolver(mount.game, plan.files())
    problems = []
    for name in sorted(models):
        if not own.find(name) or own.closure(name) == effective.closure(name):
            continue
        if lost := sorted(own.sequences(name) - effective.sequences(name)):
            shown = ", ".join(lost[:8]) + (", ..." if len(lost) > 8 else "")
            problems.append(f"{name} plays {len(lost)} fewer sequences than its own ({shown}): add its character to CAMPAIGN_CHARACTERS")
    return problems


def merged_order_problems(mount: Mount, lists: Lists) -> list[str]:
    """MERGED_MODEL_SOURCES must be the order GAME finds the campaigns' own paks in."""
    positions = []
    for path_id in lists.merged:
        where = [mount.game.index(source) for source in mount.ids.get(path_id, []) if isinstance(source, Pak) and source in mount.game]
        if not where:
            return [f"{mount.engine}: no pak of {path_id} is on GAME"]
        positions.append(min(where))
    return [] if positions == sorted(positions) else [f"{mount.engine}: GAME has the campaigns' paks in another order than MERGED_MODEL_SOURCES"]


def winner_problems(mount: Mount, planner: Planner) -> list[str]:
    """server.dll takes the merged mount's version from MERGED_MODEL_SOURCES; GAME's (hl2dm's own hl2_misc comes first
    there) must be the same."""
    problems = []
    for name in planner.own:
        assumed, real = planner.first_of(planner.lists.merged, name), first(mount.game, name)
        mine, theirs = (model_of(assumed[1], name) if assumed else None), (model_of(real, name) if real else None)
        if mine and theirs and mine.checksum != theirs.checksum:
            problems.append(f"{mount.engine}: GAME's {name} ({real.label}) isn't {assumed[0]}'s version")
    return problems


def dependents_problems(mount: Mount, lists: Lists, models: dict[str, list[Source]]) -> list[str]:
    """CAMPAIGN_MODEL_DEPENDENTS must be the models that include a swapped one, directly or not, but the swapped ones."""
    includers: dict[str, set[str]] = {}
    for name, sources in models.items():
        for source in sources:
            model = model_of(source, name)
            for include in model.includes if model else ():
                includers.setdefault(include, set()).add(name)
    stale, todo = set(lists.swapped), list(lists.swapped)
    while todo:
        for includer in includers.get(todo.pop(), set()) - stale:
            stale.add(includer)
            todo.append(includer)
    expected, listed = stale - set(lists.swapped), set(lists.dependents)
    missing = [f"{mount.engine}: {name} includes a swapped model: add it to CAMPAIGN_MODEL_DEPENDENTS" for name in sorted(expected - listed)]
    return missing + [f"{mount.engine}: {name} includes no swapped model: take it out of CAMPAIGN_MODEL_DEPENDENTS" for name in sorted(listed - expected)]


def report(mount: Mount, lists: Lists, label: str, path_ids: tuple[str, ...], plan: Plan) -> None:
    """what a set copies, and the checksums its characters show."""
    files = plan.files()
    size = sum(len(source.read(name)) for name, source in files.items())
    if path_ids:
        print(f"  {label}: {len(plan.models)} models, {len(files)} files ({size / 1e6:.1f} MB) from {', '.join(path_ids)}")
        print("    copies " + ", ".join(f"{short(name)} {own.model.checksum:08x} <- {own.path_id}" for name, own in plan.models.items()))
    else:
        print(f"  {label}: nothing copied")
    shown, own = Resolver(mount.game, files), mount.chain(path_ids) if path_ids else mount.game
    played = []
    for root in lists.characters:
        if first(own, root):
            closure = shown.closure(root)
            includes = ", ".join(f"{short(name)} {checksum:08x}" for name, checksum in closure[1:])
            played.append(f"{short(root)} {closure[0][1]:08x}" + (f" ({includes})" if includes else ""))
    print("    its maps play " + "; ".join(played))


def check_mount(mount: Mount, lists: Lists) -> tuple[list[str], set[str]]:
    """the problems with the lists under one engine's mount, and the models its sets copy."""
    print(f"{mount.engine}:")
    models = all_models(mount)
    problems = merged_order_problems(mount, lists) + dependents_problems(mount, lists, models)
    copied: set[str] = set()
    for label, path_ids in SETS.items():
        planner = Planner(mount, lists, path_ids)
        plan = planner.plan()
        problems += [f"{mount.engine}, {label}: {skipped}" for skipped in plan.skipped]
        problems += [f"{mount.engine}, {label}: {lost}" for lost in lost_sequences(mount, path_ids, plan, models)]
        problems += winner_problems(mount, planner)
        copied |= set(plan.models)
        report(mount, lists, label, path_ids, plan)
    report(mount, lists, "episode two (the merged mount)", (), Plan())
    return problems, copied


def mounts() -> list[Mount]:
    hl2 = engines.ENGINES["hl2"].find_exe().parent
    folders = {"|all_source_engine_paths|": hl2, "|appid_220|": hl2}
    found = [mount_of(engines.ENGINES["hl2"], folders)]
    try:
        folders["|appid_320|"] = engines.ENGINES["hl2dm"].find_exe().parent
    except ToolError as missing:
        print(f"hl2dm's mount not checked: {missing}")
        return found
    return [*found, mount_of(engines.ENGINES["hl2dm"], folders)]


def main() -> int:
    lists = read_lists()
    checked = mounts()
    problems: list[str] = []
    copied: set[str] = set()
    for mount in checked:
        found, by_mount = check_mount(mount, lists)
        problems += found
        copied |= by_mount
    problems += [f"{name} is in CAMPAIGN_MODELS_SWAPPED but no set copies it" for name in sorted(set(lists.swapped) - copied)]
    problems += [
        f"{name} is in CAMPAIGN_CHARACTERS but no campaign has it" for name in lists.characters if not any(first(mount.game, name) for mount in checked)
    ]
    if problems:
        print(f"\nthe lists in {HEADER.relative_to(REPO).as_posix()} need changing:")
        for problem in dict.fromkeys(problems):
            print(f"  {problem}")
        return 1
    print("\nthe lists match the installed games")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
