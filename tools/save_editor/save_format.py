"""Binary layout for Tamagotchi SaveData v5 (little-endian, packed)."""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field
from typing import List, Tuple

MAGIC = 0x54414D41  # 'TAMA'
VERSION = 5
VERSION_V4 = 4
VERSION_V3 = 3

# PetSlot: 20 bytes
PET_FMT = "<5b5bHII"
PET_SIZE = struct.calcsize(PET_FMT)
assert PET_SIZE == 20

# Header through last_unix, then 2 pets, then pace_idx, then crc
HDR_FMT = "<IHH10B8BIII"
HDR_SIZE = struct.calcsize(HDR_FMT)
assert HDR_SIZE == 38

SAVE_SIZE_V3 = HDR_SIZE + 2 * PET_SIZE + 4  # 82
SAVE_SIZE_V4 = HDR_SIZE + 2 * PET_SIZE + 1 + 4  # 83
SAVE_SIZE = SAVE_SIZE_V4 + 3  # 86, three button actions before crc
CRC_OFF_V3 = HDR_SIZE + 2 * PET_SIZE
CRC_OFF_V4 = CRC_OFF_V3 + 1
CRC_OFF = CRC_OFF_V4 + 3


@dataclass
class PetSlot:
    hunger: int = 72
    happy: int = 74
    health: int = 92
    energy: int = 84
    clean: int = 86
    head: int = 0
    outfit: int = 0
    extra: int = 0
    sleeping: int = 0
    pad: int = 0
    level: int = 1
    xp: int = 0
    age_sec: int = 0

    def pack(self) -> bytes:
        return struct.pack(
            PET_FMT,
            self.hunger,
            self.happy,
            self.health,
            self.energy,
            self.clean,
            self.head,
            self.outfit,
            self.extra,
            self.sleeping,
            self.pad,
            self.level,
            self.xp,
            self.age_sec,
        )

    @classmethod
    def unpack(cls, raw: bytes) -> "PetSlot":
        t = struct.unpack(PET_FMT, raw)
        return cls(*t)


@dataclass
class SaveData:
    magic: int = MAGIC
    version: int = VERSION
    bytes_: int = SAVE_SIZE
    active: int = 0
    started: int = 0
    touch_mode: int = 0
    theme: int = 0
    dim_idx: int = 2
    off_idx: int = 2
    bl_idx: int = 2
    led_flash: int = 1
    show_fps: int = 1
    trans_idx: int = 0
    food: List[int] = field(default_factory=lambda: [0] * 8)
    coins: int = 30
    clothes: int = 0
    last_unix: int = 0
    pet: List[PetSlot] = field(default_factory=lambda: [PetSlot(), PetSlot()])
    pace_idx: int = 0
    btn: List[int] = field(default_factory=lambda: [0, 2, 1])
    crc: int = 0

    def body_without_crc(self) -> bytes:
        food = list(self.food) + [0] * 8
        food = food[:8]
        hdr = struct.pack(
            HDR_FMT,
            self.magic,
            self.version,
            SAVE_SIZE,
            self.active & 0xFF,
            self.started & 0xFF,
            self.touch_mode & 0xFF,
            self.theme & 0xFF,
            self.dim_idx & 0xFF,
            self.off_idx & 0xFF,
            self.bl_idx & 0xFF,
            self.led_flash & 0xFF,
            self.show_fps & 0xFF,
            self.trans_idx & 0xFF,
            *food,
            self.coins & 0xFFFFFFFF,
            self.clothes & 0xFFFFFFFF,
            self.last_unix & 0xFFFFFFFF,
        )
        pace = struct.pack("B", self.pace_idx & 0xFF)
        keys = [(n & 0xFF) for n in (list(self.btn) + [0, 2, 1])[:3]]
        return hdr + self.pet[0].pack() + self.pet[1].pack() + pace + struct.pack("3B", *keys)

    def compute_crc(self) -> int:
        return zlib.crc32(self.body_without_crc()) & 0xFFFFFFFF

    def pack(self) -> bytes:
        self.magic = MAGIC
        self.version = VERSION
        self.bytes_ = SAVE_SIZE
        self.crc = self.compute_crc()
        return self.body_without_crc() + struct.pack("<I", self.crc)

    @classmethod
    def unpack(cls, raw: bytes) -> Tuple["SaveData", str | None]:
        if len(raw) < SAVE_SIZE_V3:
            return cls(), f"file too short ({len(raw)} < {SAVE_SIZE_V3})"
        hdr = struct.unpack(HDR_FMT, raw[:HDR_SIZE])
        magic, version, nbytes = hdr[0], hdr[1], hdr[2]
        pace = 0
        buttons = [0, 2, 1]
        if version == VERSION_V3:
            blob = raw[:SAVE_SIZE_V3]
            pets = [
                PetSlot.unpack(blob[HDR_SIZE : HDR_SIZE + PET_SIZE]),
                PetSlot.unpack(blob[HDR_SIZE + PET_SIZE : CRC_OFF_V3]),
            ]
            (crc,) = struct.unpack("<I", blob[CRC_OFF_V3:SAVE_SIZE_V3])
            expect = zlib.crc32(blob[:CRC_OFF_V3]) & 0xFFFFFFFF
            size_ok = nbytes == SAVE_SIZE_V3
        elif version == VERSION_V4:
            if len(raw) < SAVE_SIZE_V4:
                return cls(), f"file too short ({len(raw)} < {SAVE_SIZE_V4})"
            blob = raw[:SAVE_SIZE_V4]
            pets = [
                PetSlot.unpack(blob[HDR_SIZE : HDR_SIZE + PET_SIZE]),
                PetSlot.unpack(blob[HDR_SIZE + PET_SIZE : CRC_OFF_V3]),
            ]
            pace = blob[CRC_OFF_V3]
            (crc,) = struct.unpack("<I", blob[CRC_OFF_V4:SAVE_SIZE_V4])
            expect = zlib.crc32(blob[:CRC_OFF_V4]) & 0xFFFFFFFF
            size_ok = nbytes == SAVE_SIZE_V4
        elif version == VERSION:
            if len(raw) < SAVE_SIZE:
                return cls(), f"file too short ({len(raw)} < {SAVE_SIZE})"
            blob = raw[:SAVE_SIZE]
            pets = [
                PetSlot.unpack(blob[HDR_SIZE : HDR_SIZE + PET_SIZE]),
                PetSlot.unpack(blob[HDR_SIZE + PET_SIZE : CRC_OFF_V3]),
            ]
            pace = blob[CRC_OFF_V3]
            buttons = list(blob[CRC_OFF_V4:CRC_OFF])
            (crc,) = struct.unpack("<I", blob[CRC_OFF:SAVE_SIZE])
            expect = zlib.crc32(blob[:CRC_OFF]) & 0xFFFFFFFF
            size_ok = nbytes == SAVE_SIZE
        else:
            return cls(), f"unsupported version {version}"
        food = list(hdr[13:21])
        s = cls(
            magic=magic,
            version=version,
            bytes_=nbytes,
            active=hdr[3],
            started=hdr[4],
            touch_mode=hdr[5],
            theme=hdr[6],
            dim_idx=hdr[7],
            off_idx=hdr[8],
            bl_idx=hdr[9],
            led_flash=hdr[10],
            show_fps=hdr[11],
            trans_idx=hdr[12],
            food=food,
            coins=hdr[21],
            clothes=hdr[22],
            last_unix=hdr[23],
            pet=pets,
            pace_idx=pace,
            btn=buttons,
            crc=crc,
        )
        err = None
        if magic != MAGIC:
            err = f"bad magic 0x{magic:08X}"
        elif not size_ok:
            err = f"bad size field {nbytes}"
        elif expect != crc:
            err = f"CRC mismatch (got 0x{crc:08X}, expect 0x{expect:08X})"
        return s, err


def load_path(path: str) -> Tuple[SaveData, str | None]:
    with open(path, "rb") as f:
        return SaveData.unpack(f.read())


def save_path(path: str, data: SaveData) -> None:
    with open(path, "wb") as f:
        f.write(data.pack())
