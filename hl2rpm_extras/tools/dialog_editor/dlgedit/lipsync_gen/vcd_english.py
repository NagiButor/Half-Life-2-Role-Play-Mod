#!/usr/bin/env python3
"""
Valve-Quality VCD Scene Generator (Pro Lipsync Edition)
=====================================================
Generates professional-quality .vcd choreography files matching Valve's standards.

Features:
- Text-to-phoneme lipsync using CMU Pronouncing Dictionary
- Realistic phoneme timing (vowels long, stops short)
- Exports Faceposer-compatible .txt phoneme data
- Smooth facial animations with attack/sustain/release curves
- Proper Valve VCD syntax (combo, range modifiers)
- Automatic blink and eye movement generation
- Parses both 'nodes' and 'returning_choreo' from dialog files

Author: Auto-generated for hl2rpm mod
"""

from __future__ import annotations

import argparse
import math
import random
import re
import sys
import struct
import wave
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union

# ============================================================================
# CONFIGURATION
# ============================================================================

DEFAULT_ACTOR = "demo_npc_1"
DEFAULT_SOUNDDIR = Path(r"E:\Steam\steamapps\sourcemods\hl2rpm\sound\demo_npc_1")
DEFAULT_SCENEDIR = Path(r"E:\Steam\steamapps\sourcemods\hl2rpm\scenes\demo_npc_1")
DEFAULT_DIALOGPATH = Path(r"E:\Steam\steamapps\sourcemods\hl2rpm\scripts\dialogs\demo_npc_1.txt")

# ============================================================================
# CMU PRONOUNCING DICTIONARY (SUBSET) & TIMING
# ============================================================================

CMUDICT: Dict[str, List[str]] = {
    # Common words
    "a": ["AH0"], "the": ["DH", "AH0"], "to": ["T", "UW1"], "you": ["Y", "UW1"],
    "i": ["AY1"], "is": ["IH1", "Z"], "it": ["IH1", "T"], "and": ["AE1", "N", "D"],
    "that": ["DH", "AE1", "T"], "was": ["W", "AA1", "Z"], "for": ["F", "AO1", "R"],
    "on": ["AA1", "N"], "are": ["AA1", "R"], "with": ["W", "IH1", "DH"],
    "they": ["DH", "EY1"], "be": ["B", "IY1"], "at": ["AE1", "T"],
    "one": ["W", "AH1", "N"], "have": ["HH", "AE1", "V"], "this": ["DH", "IH1", "S"],
    "from": ["F", "R", "AH1", "M"], "or": ["AO1", "R"], "had": ["HH", "AE1", "D"],
    "by": ["B", "AY1"], "but": ["B", "AH1", "T"], "not": ["N", "AA1", "T"],
    "what": ["W", "AH1", "T"], "all": ["AO1", "L"], "were": ["W", "ER1"],
    "we": ["W", "IY1"], "when": ["W", "EH1", "N"], "your": ["Y", "AO1", "R"],
    "can": ["K", "AE1", "N"], "said": ["S", "EH1", "D"], "there": ["DH", "EH1", "R"],
    "an": ["AE1", "N"], "each": ["IY1", "CH"], "which": ["W", "IH1", "CH"],
    "do": ["D", "UW1"], "how": ["HH", "AW1"], "their": ["DH", "EH1", "R"],
    "if": ["IH1", "F"], "will": ["W", "IH1", "L"], "up": ["AH1", "P"],
    "about": ["AH0", "B", "AW1", "T"], "out": ["AW1", "T"], "many": ["M", "EH1", "N", "IY0"],
    "then": ["DH", "EH1", "N"], "them": ["DH", "EH1", "M"], "these": ["DH", "IY1", "Z"],
    "so": ["S", "OW1"], "some": ["S", "AH1", "M"], "her": ["HH", "ER1"],
    "would": ["W", "UH1", "D"], "make": ["M", "EY1", "K"], "like": ["L", "AY1", "K"],
    "him": ["HH", "IH1", "M"], "into": ["IH1", "N", "T", "UW0"], "time": ["T", "AY1", "M"],
    "has": ["HH", "AE1", "Z"], "look": ["L", "UH1", "K"], "two": ["T", "UW1"],
    "more": ["M", "AO1", "R"], "go": ["G", "OW1"], "see": ["S", "IY1"],
    "no": ["N", "OW1"], "way": ["W", "EY1"], "could": ["K", "UH1", "D"],
    "my": ["M", "AY1"], "than": ["DH", "AE1", "N"], "first": ["F", "ER1", "S", "T"],
    "been": ["B", "IH1", "N"], "call": ["K", "AO1", "L"], "who": ["HH", "UW1"],
    "now": ["N", "AW1"], "find": ["F", "AY1", "N", "D"], "long": ["L", "AO1", "NG"],
    "down": ["D", "AW1", "N"], "day": ["D", "EY1"], "get": ["G", "EH1", "T"],
    "come": ["K", "AH1", "M"], "made": ["M", "EY1", "D"], "may": ["M", "EY1"],
    "part": ["P", "AA1", "R", "T"], "over": ["OW1", "V", "ER0"],
    
    # Game/action words
    "kill": ["K", "IH1", "L"], "die": ["D", "AY1"], "dead": ["D", "EH1", "D"],
    "attack": ["AH0", "T", "AE1", "K"], "fire": ["F", "AY1", "R"],
    "enemy": ["EH1", "N", "AH0", "M", "IY0"], "danger": ["D", "EY1", "N", "JH", "ER0"],
    "help": ["HH", "EH1", "L", "P"], "stop": ["S", "T", "AA1", "P"],
    "run": ["R", "AH1", "N"], "go": ["G", "OW1"], "move": ["M", "UW1", "V"],
    "watch": ["W", "AA1", "CH"], "fight": ["F", "AY1", "T"],
    "gun": ["G", "AH1", "N"], "shoot": ["SH", "UW1", "T"],
    
    # Dialog-specific words
    "got": ["G", "AA1", "T"], "ten": ["T", "EH1", "N"],
    "seconds": ["S", "EH1", "K", "AH0", "N", "D", "Z"],
    "state": ["S", "T", "EY1", "T"], "business": ["B", "IH1", "Z", "N", "AH0", "S"],
    "before": ["B", "IH0", "F", "AO1", "R"], "decide": ["D", "IH0", "S", "AY1", "D"],
    "face": ["F", "EY1", "S"], "looks": ["L", "UH1", "K", "S"],
    "better": ["B", "EH1", "T", "ER0"], "hole": ["HH", "OW1", "L"],
    "resistance": ["R", "IH0", "Z", "IH1", "S", "T", "AH0", "N", "S"],
    "cell": ["S", "EH1", "L"], "keep": ["K", "IY1", "P"],
    "alive": ["AH0", "L", "AY1", "V"], "situation": ["S", "IH2", "CH", "UW0", "EY1", "SH", "AH0", "N"],
    "nightmare": ["N", "AY1", "T", "M", "EH2", "R"], "week": ["W", "IY1", "K"],
    "ago": ["AH0", "G", "OW1"], "combine": ["K", "AA1", "M", "B", "AY2", "N"],
    "sealed": ["S", "IY1", "L", "D"], "lower": ["L", "OW1", "ER0"],
    "block": ["B", "L", "AA1", "K"], "sewers": ["S", "UW1", "ER0", "Z"],
    "buffet": ["B", "AH0", "F", "EY1"], "head": ["HH", "EH1", "D"],
    "hopping": ["HH", "AA1", "P", "IH0", "NG"], "freaks": ["F", "R", "IY1", "K", "S"],
    "headcrabs": ["HH", "EH1", "D", "K", "R", "AE2", "B", "Z"],
    "lazy": ["L", "EY1", "Z", "IY0"], "ones": ["W", "AH1", "N", "Z"],
    "talking": ["T", "AO1", "K", "IH0", "NG"], "nasty": ["N", "AE1", "S", "T", "IY0"],
    "little": ["L", "IH1", "T", "AH0", "L"], "sewer": ["S", "UW1", "ER0"],
    "fiends": ["F", "IY1", "N", "D", "Z"], "kind": ["K", "AY1", "N", "D"],
    "crawling": ["K", "R", "AO1", "L", "IH0", "NG"],
    "nightmares": ["N", "AY1", "T", "M", "EH2", "R", "Z"],
    "pulse": ["P", "AH1", "L", "S"], "flatline": ["F", "L", "AE1", "T", "L", "AY2", "N"],
    "breathing": ["B", "R", "IY1", "DH", "IH0", "NG"], "near": ["N", "IH1", "R"],
    "cull": ["K", "AH1", "L"], "boys": ["B", "OY1", "Z"],
    "gonna": ["G", "AA1", "N", "AH0"], "wake": ["W", "EY1", "K"],
    "screaming": ["S", "K", "R", "IY1", "M", "IH0", "NG"],
    "meat": ["M", "IY1", "T"], "puppets": ["P", "AH1", "P", "AH0", "T", "S"],
    "thrashing": ["TH", "R", "AE1", "SH", "IH0", "NG"],
    "own": ["OW1", "N"], "skin": ["S", "K", "IH1", "N"],
    "three": ["TH", "R", "IY1"], "those": ["DH", "OW1", "Z"],
    "real": ["R", "IY1", "L"], "hungry": ["HH", "AH1", "NG", "G", "R", "IY0"],
    "eager": ["IY1", "G", "ER0"], "turn": ["T", "ER1", "N"],
    "inside": ["IH1", "N", "S", "AY2", "D"],
    "spirit": ["S", "P", "IH1", "R", "IH0", "T"],
    "paint": ["P", "EY1", "N", "T"], "concrete": ["K", "AA1", "N", "K", "R", "IY2", "T"],
    "rest": ["R", "EH1", "S", "T"], "crawl": ["K", "R", "AO1", "L"],
    "back": ["B", "AE1", "K"], "whatever": ["W", "AH2", "T", "EH1", "V", "ER0"],
    "spawned": ["S", "P", "AO1", "N", "D"], "god": ["G", "AA1", "D"],
    "sake": ["S", "EY1", "K"], "shadows": ["SH", "AE1", "D", "OW0", "Z"],
    "bite": ["B", "AY1", "T"],
    "already": ["AO0", "L", "R", "EH1", "D", "IY0"], "danced": ["D", "AE1", "N", "S", "T"],
    "dance": ["D", "AE1", "N", "S"], "don't": ["D", "OW1", "N", "T"],
    "regret": ["R", "IH0", "G", "R", "EH1", "T"], "shooting": ["SH", "UW1", "T", "IH0", "NG"],
    "sight": ["S", "AY1", "T"], "we": ["W", "IY1"],
    "beautiful": ["B", "Y", "UW1", "T", "AH0", "F", "AH0", "L"],
    "regular": ["R", "EH1", "G", "Y", "AH0", "L", "ER0"],
    "professional": ["P", "R", "AH0", "F", "EH1", "SH", "AH0", "N", "AH0", "L"],
    "psychopath": ["S", "AY1", "K", "AH0", "P", "AE2", "TH"],
    "take": ["T", "EY1", "K"], "morphine": ["M", "AO1", "R", "F", "IY0", "N"],
    "shot": ["SH", "AA1", "T"], "heart": ["HH", "AA1", "R", "T"],
    "sleep": ["S", "L", "IY1", "P"], "luxury": ["L", "AH1", "G", "ZH", "ER0", "IY0"],
    "bullets": ["B", "UH1", "L", "AH0", "T", "S"], "bad": ["B", "AE1", "D"],
    "news": ["N", "UW1", "Z"], "only": ["OW1", "N", "L", "IY0"],
    "things": ["TH", "IH1", "NG", "Z"], "show": ["SH", "OW1"],
    "trust": ["T", "R", "AH1", "S", "T"], "me": ["M", "IY1"],
    "wanted": ["W", "AA1", "N", "T", "IH0", "D"], "having": ["HH", "AE1", "V", "IH0", "NG"],
    "much": ["M", "AH1", "CH"], "shorter": ["SH", "AO1", "R", "T", "ER0"],
    "conversation": ["K", "AA2", "N", "V", "ER0", "S", "EY1", "SH", "AH0", "N"],
    "welcome": ["W", "EH1", "L", "K", "AH0", "M"],
    "sweetheart": ["S", "W", "IY1", "T", "HH", "AA2", "R", "T"],
    "shovel": ["SH", "AH1", "V", "AH0", "L"], "someone": ["S", "AH1", "M", "W", "AH2", "N"],
    "else": ["EH1", "L", "S"], "garbage": ["G", "AA1", "R", "B", "IH0", "JH"],
}

# Standard relative durations for phonemes (seconds)
PHONEME_DURATIONS: Dict[str, float] = {
    # Vowels (Realistic SAPI-like)
    "AA": 0.09, "AE": 0.08, "AH": 0.07, "AO": 0.09, "AW": 0.11, "AY": 0.11,
    "EH": 0.07, "ER": 0.08, "EY": 0.11, "IH": 0.05, "IY": 0.07, "OW": 0.11,
    "OY": 0.11, "UH": 0.07, "UW": 0.09,
    # Consonants (Snappy)
    "P": 0.02, "B": 0.02, "T": 0.02, "D": 0.02, "K": 0.03, "G": 0.03,
    "F": 0.03, "V": 0.03, "TH": 0.04, "DH": 0.04, "S": 0.05, "Z": 0.05,
    "SH": 0.06, "ZH": 0.06, "CH": 0.06, "JH": 0.06, "M": 0.04, "N": 0.04,
    "NG": 0.05, "L": 0.04, "R": 0.04, "W": 0.05, "Y": 0.05, "HH": 0.04,
    # Silence / Pause
    "SIL": 0.05,
}

# ============================================================================
# PHONEME TO VISEME MAPPING
# ============================================================================

# ============================================================================
# PHONEME TO VISEME MAPPING
# ============================================================================

@dataclass
class Viseme:
    """Represents mouth shape for a phoneme."""
    jaw_drop: float = 0.0
    part: float = 0.0
    puckerer: float = 0.0
    funneler: float = 0.0
    corner_puller: float = 0.0
    stretcher: float = 0.0
    presser: float = 0.0
    bite: float = 0.0
    upper_raiser: float = 0.0
    chin_raiser: float = 0.0

VISEME_MAP: Dict[str, Viseme] = {
    # Vowels - Wide open
    "AA": Viseme(jaw_drop=0.75, part=0.5),
    "AE": Viseme(jaw_drop=0.65, part=0.4, stretcher=0.2),
    "AH": Viseme(jaw_drop=0.50, part=0.3),
    "AO": Viseme(jaw_drop=0.60, part=0.4, puckerer=0.2),
    "AW": Viseme(jaw_drop=0.55, part=0.35, puckerer=0.3),
    "AY": Viseme(jaw_drop=0.50, part=0.3, stretcher=0.15),
    
    # Vowels - Rounded
    "OW": Viseme(jaw_drop=0.40, puckerer=0.5, funneler=0.3),
    "OY": Viseme(jaw_drop=0.45, puckerer=0.4, funneler=0.25),
    "UW": Viseme(jaw_drop=0.25, puckerer=0.6, funneler=0.4),
    "UH": Viseme(jaw_drop=0.30, puckerer=0.4),
    
    # Vowels - Smile/front
    "EY": Viseme(jaw_drop=0.35, corner_puller=0.35, stretcher=0.25),
    "EH": Viseme(jaw_drop=0.40, corner_puller=0.25, stretcher=0.2),
    "IY": Viseme(jaw_drop=0.20, corner_puller=0.45, stretcher=0.35),
    "IH": Viseme(jaw_drop=0.25, corner_puller=0.30, stretcher=0.20),
    
    # R-colored vowels
    "ER": Viseme(jaw_drop=0.30, puckerer=0.25),
    
    # Bilabial stops/nasals - Closed lips
    "M": Viseme(jaw_drop=0.05, presser=0.7),
    "B": Viseme(jaw_drop=0.05, presser=0.65),
    "P": Viseme(jaw_drop=0.05, presser=0.7),
    
    # Labiodental - Teeth on lip
    "F": Viseme(jaw_drop=0.15, bite=0.5, upper_raiser=0.2),
    "V": Viseme(jaw_drop=0.15, bite=0.45, upper_raiser=0.15),
    
    # Dental/alveolar
    "TH": Viseme(jaw_drop=0.20, part=0.15),
    "DH": Viseme(jaw_drop=0.20, part=0.15),
    "T": Viseme(jaw_drop=0.25, part=0.2),
    "D": Viseme(jaw_drop=0.25, part=0.2),
    "N": Viseme(jaw_drop=0.20, part=0.15),
    "L": Viseme(jaw_drop=0.30, part=0.2),
    "S": Viseme(jaw_drop=0.15, stretcher=0.35, part=0.1),
    "Z": Viseme(jaw_drop=0.15, stretcher=0.30, part=0.1),
    
    # Postalveolar
    "SH": Viseme(jaw_drop=0.20, puckerer=0.3, funneler=0.2),
    "ZH": Viseme(jaw_drop=0.20, puckerer=0.25, funneler=0.15),
    "CH": Viseme(jaw_drop=0.22, puckerer=0.25),
    "JH": Viseme(jaw_drop=0.22, puckerer=0.2),
    
    # Velar/glottal
    "K": Viseme(jaw_drop=0.25),
    "G": Viseme(jaw_drop=0.25),
    "NG": Viseme(jaw_drop=0.20),
    "HH": Viseme(jaw_drop=0.30, part=0.2),
    
    # Approximants
    "R": Viseme(jaw_drop=0.25, puckerer=0.2),
    "Y": Viseme(jaw_drop=0.20, corner_puller=0.3, stretcher=0.2),
    "W": Viseme(jaw_drop=0.15, puckerer=0.55, funneler=0.35),
}

DEFAULT_VISEME = Viseme(jaw_drop=0.20)

# RICH EMOTION PROFILES (Alyx-like)
EMOTION_PROFILES: Dict[str, Dict[str, float]] = {
    "neutral": {
        "inner_raiser": 0.1,    # Slight concern/focus
        "lid_tightener": 0.15,  # Alertness
        "cheek_raiser": 0.05,   # Not dead
        "lowerer": 0.1,
    },
    "threat": {
        "inner_raiser": 0.4,
        "lowerer": 0.8,         # Deep frown
        "lid_tightener": 0.6,   # Intense stare
        "wrinkler": 0.5,        # Disgust/Aggression
        "cheek_raiser": 0.2,
        "jaw_clenched": 0.4,
    },
    "question": {
        "inner_raiser": 0.7,    # Brows up
        "outer_raiser": 0.5,
        "lid_tightener": 0.2,
        "lowerer": 0.1,
        "wrinkler": 0.2,
    },
    "agree": {
        "cheek_raiser": 0.6,    # Smile
        "corner_puller": 0.4,
        "outer_raiser": 0.4,    # Happy brows
        "lid_tightener": 0.3,   # Duchenne smile
        "inner_raiser": 0.2,
    },
    "disagree": {
        "lowerer": 0.6,
        "wrinkler": 0.5,        # Scrunched nose
        "lid_tightener": 0.4,
        "corner_depressor": 0.3, # Frown
        "chin_raiser": 0.3,
    },
    "sad": {
        "inner_raiser": 0.8,    # Classic sad brows
        "lowerer": 0.2,
        "corner_depressor": 0.4,
        "lid_droop": 0.3,
    },
    "angry": {
        "lowerer": 0.9,
        "lid_tightener": 0.7,
        "wrinkler": 0.6,
        "dilator": 0.5,         # Nostrils
        "jaw_clenched": 0.5,
    },
}

EMOTION_KEYWORDS: Dict[str, List[str]] = {
    "threat": ["kill", "die", "dead", "attack", "fire", "enemy", "danger", "warn", "shoot", "gun", "fight"],
    "question": ["?", "why", "what", "where", "how", "who", "when", "which"],
    "agree": ["yes", "yeah", "ok", "okay", "sure", "right", "correct", "good", "fine", "cool", "nice"],
    "disagree": ["no", "nope", "never", "wrong", "can't", "don't", "won't", "not", "stop"],
    "sad": ["sorry", "sad", "miss", "lost", "gone", "dead", "died", "hurt", "pain"],
    "angry": ["damn", "hell", "hate", "angry", "bastard", "idiot", "stupid"],
}

@dataclass
class Config:
    """Configuration for VCD generation."""
    sound_dir: Path
    scene_dir: Path
    dialog_path: Path
    actor: str
    fps: int = 60
    overwrite: bool = True

@dataclass
class DialogNode:
    """Represents a single dialogue node."""
    node_id: str
    text: str
    choreo_path: str
    wav_path: Path
    duration: float
    sound_rel: str

@dataclass
class Keyframe:
    """Single animation keyframe."""
    time: float
    value: float

# ============================================================================
# TEXT TO PHONEME CONVERSION
# ============================================================================

def clean_word(word: str) -> str:
    """Remove punctuation from word."""
    return re.sub(r"[^a-zA-Z']", "", word).lower()

def fallback_g2p(word: str) -> List[str]:
    """Simple rule-based G2P."""
    word = word.lower()
    phonemes = []
    i = 0
    while i < len(word):
        c = word[i]
        next_c = word[i+1] if i+1 < len(word) else ""
        if c == "t" and next_c == "h": phonemes.append("TH"); i += 2; continue
        elif c == "s" and next_c == "h": phonemes.append("SH"); i += 2; continue
        elif c == "c" and next_c == "h": phonemes.append("CH"); i += 2; continue
        elif c == "n" and next_c == "g": phonemes.append("NG"); i += 2; continue
        
        mapping = {
            "a": "AE1", "e": "EH1", "i": "IH1", "o": "AA1", "u": "AH1",
            "b": "B", "c": "K", "d": "D", "f": "F", "g": "G", "h": "HH", 
            "j": "JH", "k": "K", "l": "L", "m": "M", "n": "N", "p": "P", 
            "q": "K", "r": "R", "s": "S", "t": "T", "v": "V", "w": "W", 
            "x": "K", "y": "Y", "z": "Z",
        }
        if c in mapping: phonemes.append(mapping[c])
        i += 1
    return phonemes if phonemes else ["AH1"]

def text_to_phonemes(text: str) -> List[str]:
    """Convert text to phonemes."""
    words = text.split()
    phonemes = []
    for word in words:
        clean = clean_word(word)
        if not clean: continue
        if clean in CMUDICT: phonemes.extend(CMUDICT[clean])
        else: phonemes.extend(fallback_g2p(clean))
    return phonemes

def get_viseme(phoneme: str) -> Viseme:
    base = re.sub(r"[012]", "", phoneme)
    return VISEME_MAP.get(base, DEFAULT_VISEME)

def get_phoneme_duration(phoneme: str) -> float:
    base = re.sub(r"[012]", "", phoneme)
    return PHONEME_DURATIONS.get(base, 0.08)

def detect_emotion(text: str) -> str:
    text_lower = text.lower()
    scores = defaultdict(int)
    for emotion, keywords in EMOTION_KEYWORDS.items():
        for kw in keywords:
            if kw in text_lower: scores[emotion] += 1
    if not scores: return "neutral"
    return max(scores.keys(), key=lambda e: scores[e])

# ============================================================================
# ANIMATION
# ============================================================================

def generate_smooth_curve(duration: float, peak_value: float, attack_time=0.2, release_time=0.25, noise_amount=0.08, rng=None) -> List[Keyframe]:
    if rng is None: rng = random.Random()
    keyframes = [Keyframe(0.0, 0.0), Keyframe(attack_time * 0.5, peak_value * 0.3), Keyframe(attack_time, peak_value * 0.85)]
    sustain_start = attack_time
    sustain_end = duration - release_time
    if sustain_end > sustain_start:
        t = sustain_start + 0.3
        while t < sustain_end:
            noise = rng.uniform(-noise_amount, noise_amount)
            value = max(0.0, min(1.0, peak_value + noise))
            keyframes.append(Keyframe(t, value))
            t += rng.uniform(0.3, 0.7)
    if release_time > 0:
        keyframes.append(Keyframe(duration - release_time, peak_value * 0.6))
        keyframes.append(Keyframe(duration - release_time * 0.3, peak_value * 0.2))
    keyframes.append(Keyframe(duration, 0.0))
    return keyframes

def generate_background_noise(duration: float, intensity: float, rng=None) -> List[Keyframe]:
    """Generates random low-intensity noise for a flex controller."""
    if rng is None: rng = random.Random()
    keyframes = [Keyframe(0.0, rng.uniform(0, intensity))]
    t = 0.0
    while t < duration:
        t += rng.uniform(0.5, 1.5)
        if t > duration: break
        val = rng.uniform(0.0, intensity)
        keyframes.append(Keyframe(t, val))
    keyframes.append(Keyframe(duration, 0.0))
    return keyframes

def generate_blink_keyframes(duration: float, rng: random.Random) -> List[Keyframe]:
    keyframes = []
    t = rng.uniform(0.5, 2.0)
    while t < duration - 0.2:
        blink_dur = rng.uniform(0.12, 0.18)
        keyframes.append(Keyframe(t, 0.0))
        keyframes.append(Keyframe(t + blink_dur * 0.4, 1.0))
        keyframes.append(Keyframe(t + blink_dur, 0.0))
        t += rng.uniform(2.0, 5.0)
    return keyframes

def generate_eye_movement(duration: float, rng: random.Random, axis: str = "rightleft") -> List[Keyframe]:
    keyframes = []
    t = 0.0
    current_value = 0.5
    while t < duration:
        keyframes.append(Keyframe(t, current_value))
        hold_time = rng.uniform(0.4, 1.5)
        t += hold_time
        if t >= duration: break
        shift = rng.uniform(-0.15, 0.15) if axis == "rightleft" else rng.uniform(-0.1, 0.1)
        current_value = max(0.3, min(0.7, 0.5 + shift))
        t += 0.08
    keyframes.append(Keyframe(duration, 0.5))
    return keyframes

def generate_head_movement(duration: float, emotion: str, rng: random.Random, axis: str = "updown") -> List[Keyframe]:
    keyframes = []
    base_value = 0.55 if emotion == "question" and axis == "tilt" else 0.5
    amplitude = 0.12 if emotion == "agree" and axis == "updown" else 0.08
    t = 0.0
    while t < duration:
        noise = rng.uniform(-amplitude, amplitude)
        value = max(0.35, min(0.65, base_value + noise))
        keyframes.append(Keyframe(t, value))
        t += rng.uniform(0.4, 0.9)
    keyframes.append(Keyframe(duration, 0.5))
    return keyframes

# ============================================================================
# LIPSYNC GENERATION (PRO)
# ============================================================================

# ============================================================================
# WAV MODIFICATION (PHONEME EMBEDDING)
# ============================================================================

def create_faceposer_data(node: DialogNode, phonemes: List[str], durations: List[float]) -> str:
    """Generate the plaintext data for Faceposer VDAT chunk (Internal Format)."""
    # Phoneme Class IDs (Source Engine / Lipsinc)
    # Mapping based on Valve Developer Community
    PHONEME_IDS = {
        'sil': 95, 'aa2': 97, 'b': 98, 'd': 100, 'ey': 101, 'f': 102, 'g': 103,
        'hh': 104, 'iy': 105, 'y': 106, 'c': 107, 'l': 108, 'm': 109, 'n': 110,
        'ow': 111, 'p': 112, 'r2': 114, 's': 115, 't': 116, 'uw': 117, 'v': 118,
        'w': 119, 'z': 122, 'ae': 230, 'dh': 240, 'nx': 331, 'aa': 593, 'ao': 596,
        'ax': 601, 'er': 602, 'eh': 603, 'ax2': 604, 'er2': 605, 'g2': 609,
        'hh2': 614, 'ih2': 616, 'ih': 618, 'l2': 619, 'r': 633, 'r3': 635,
        'd2': 638, 'sh': 643, 'uh': 650, 'ah': 652, 'zh': 658, 'jh': 676,
        'ch': 679, 'th': 952
    }
    
    def get_id(ph_code):
        # Clean code
        code = re.sub(r"[012]", "", ph_code).lower()
        if code in PHONEME_IDS: return PHONEME_IDS[code]
        # Fallback to ASCII of first char if no map (better than 0)
        return ord(code[0]) if code else 95

    # Use CRLF for Windows compatibility
    nl = "\r\n"
    lines = []
    lines.append("VERSION 1.0")
    
    # PLAINTEXT
    lines.append("PLAINTEXT")
    lines.append("{")
    lines.append(f'"{node.text}"')
    lines.append("}")
    
    # WORDS
    lines.append("WORDS")
    lines.append("{")
    
    words = node.text.split()
    word_phoneme_counts = []
    for w in words:
        p = text_to_phonemes(w)
        if p: word_phoneme_counts.append((w, len(p)))
    
    ph_idx = 0
    start_t = 0.0
    
    for word_text, count in word_phoneme_counts:
        if ph_idx >= len(durations): break
        w_dur = sum(durations[ph_idx : ph_idx + count])
        end_t = start_t + w_dur
        
        # WORD text start end (NO QUOTES on text)
        lines.append(f'WORD {word_text} {start_t:.6f} {end_t:.6f}')
        lines.append("{")
        
        w_curr_t = start_t
        for i in range(count):
            if ph_idx >= len(phonemes): break
            p = phonemes[ph_idx]
            d = durations[ph_idx]
            clean_p = re.sub(r"[012]", "", p).lower()
            pid = get_id(p)
            
            # ID Code Start End Volume (NO "PHONEME" keyword, NO QUOTES on code)
            lines.append(f'{pid} {clean_p} {w_curr_t:.6f} {w_curr_t + d:.6f} 1')
            
            w_curr_t += d
            ph_idx += 1
            
        lines.append("}")
        start_t = end_t
    
    lines.append("}")
        
    # EMPHASIS
    lines.append("EMPHASIS")
    lines.append("{")
    lines.append("}")
    
    # CLOSECAPTION
    lines.append("CLOSECAPTION")
    lines.append("{")
    lines.append("}")
    
    # OPTIONS
    lines.append("OPTIONS")
    lines.append("{")
    lines.append("voice_duck 0")
    lines.append("}")
        
    return nl.join(lines)

def embed_wav_phonemes(wav_path: Path, phoneme_data: str) -> None:
    """Embeds phoneme text data into the WAV file using a 'VDAT' chunk."""
    if not wav_path.exists(): return
    
    try:
        import struct
        
        with open(wav_path, "rb") as f:
            data = bytearray(f.read())
            
        if data[:4] != b'RIFF':
            print(f"[WARN] {wav_path.name} is not a valid RIFF WAV.")
            return

        # Prepare new VDAT chunk
        # NOTE: Must be null-terminated C-string logic often used in Source
        chunk_name = b'VDAT'
        chunk_data = phoneme_data.encode('ascii', errors='ignore') + b'\0'
        chunk_len = len(chunk_data)
        
        # Pad to even length
        pad = b''
        if chunk_len % 2 == 1:
            pad = b'\0'
            
        new_chunk = chunk_name + struct.pack('<I', chunk_len) + chunk_data + pad
        
        # Reconstruct WAV excluding old VDAT chunks
        new_wav = bytearray()
        new_wav.extend(data[:12]) # RIFF header + WAVE
        
        idx = 12
        total_len = len(data)
        
        while idx < total_len:
            if idx + 8 > total_len: break # Incomplete header
            
            c_id = data[idx:idx+4]
            c_size = struct.unpack('<I', data[idx+4:idx+8])[0]
            
            # Helper to calculate padded size for traversal
            padded_size = c_size + (1 if c_size % 2 == 1 else 0)
            
            if c_id == b'VDAT':
                # Skip this chunk
                idx += 8 + padded_size
            else:
                # Copy this chunk
                chunk_end = idx + 8 + padded_size
                if chunk_end > total_len: break # Corrupt
                new_wav.extend(data[idx:chunk_end])
                idx = chunk_end
                
        # Append new VDAT chunk
        new_wav.extend(new_chunk)
        
        # Update RIFF size (File size - 8)
        new_total_size = len(new_wav) - 8
        new_wav[4:8] = struct.pack('<I', new_total_size)
        
        with open(wav_path, "wb") as f:
            f.write(new_wav)
            
        print(f"[WAV] Replaced VDAT chunk in {wav_path.name}")
        
    except Exception as e:
        print(f"[ERR] Failed to modify WAV {wav_path.name}: {e}")

# ============================================================================
# GESTURES (Standard HL2 male/female)
# ============================================================================

GESTURE_MAP: Dict[str, List[str]] = {
    "neutral": ["g_fist", "g_point", "g_shrug", "g_answer", "g_explain"],
    "threat": ["g_fist", "g_point", "gesture_agitated_1"],
    "question": ["g_shrug", "g_question", "g_query"],
    "agree": ["g_nod", "gesture_agree", "g_present"],
    "disagree": ["g_headshake", "gesture_disagree", "g_refuse"],
    "sad": ["g_plead", "g_despair"],
    "angry": ["g_fist", "gesture_agitated_1", "g_frustrated"],
}

def get_context_aware_gesture(text: str, emotion: str, duration: float, rng: random.Random) -> Optional[str]:
    """Selects a gesture based on context, duration, and randomness."""
    # 1. Duration Check
    if duration < 1.0: return None # Too short for gesture
    
    # 2. Context Keywords (with probability caps to prevent spam)
    text_lower = text.lower()
    
    # "Over there" -> Point (High chance)
    if any(k in text_lower for k in ["over there", "look", "see that"]): 
        return "g_point" if rng.random() < 0.7 else None
        
    # "Hi" -> Wave (Low chance, very annoying if spammed)
    if any(k in text_lower for k in ["hi ", "hello", "hey"]): 
        return "g_wave" if rng.random() < 0.25 else None
        
    # "Dunno" -> Shrug
    if any(k in text_lower for k in ["i don't know", "maybe", "perhaps", "dunno"]): 
        return "g_shrug" if rng.random() < 0.5 else None
    
    # 3. Probability Check based on Emotion
    prob = {
        "neutral": 0.3, # Reduce neutral gestures
        "agree": 0.5,
        "disagree": 0.5,
        "question": 0.6,
        "angry": 0.7,
        "threat": 0.7
    }.get(emotion, 0.4)
    
    if rng.random() > prob: return None
    
    # 4. Random Selection
    options = GESTURE_MAP.get(emotion, GESTURE_MAP["neutral"])
    if not options: return None
    return rng.choice(options)

# ... (skip to build_flexanimation_block)

def scan_audio_peaks(wav_path: Path, min_dist=0.3) -> List[float]:
    """Finds timestamps of amplitude peaks (accents)."""
    peaks = []
    try:
        with wave.open(str(wav_path), "rb") as wf:
            params = wf.getparams()
            if params.sampwidth != 2: return []
            fr = params.framerate
            n_frames = params.nframes
            raw = wf.readframes(n_frames)
            samples = struct.unpack(f"<{n_frames * params.nchannels}h", raw)
            
            # Downsample for envelope
            chunk = int(fr * 0.05) # 50ms
            envelope = []
            for i in range(0, len(samples), chunk * params.nchannels):
                s_chunk = samples[i : i + chunk * params.nchannels]
                rms = math.sqrt(sum(s*s for s in s_chunk) / len(s_chunk))
                envelope.append((i / params.nchannels / fr, rms))
            
            # Find local maxima
            max_amp = max(e[1] for e in envelope) if envelope else 0
            threshold = max_amp * 0.6
            
            last_peak = -1.0
            for t, amp in envelope:
                if amp > threshold:
                    if last_peak < 0 or (t - last_peak) > min_dist:
                        peaks.append(t)
                        last_peak = t
    except: pass
    return peaks

def generate_accented_curve(duration: float, peaks: List[float], base_val: float, rng: random.Random) -> List[Keyframe]:
    """Generates a curve that spikes at audio peaks."""
    keyframes = [Keyframe(0.0, base_val * 0.5)]
    
    # Sort peaks
    sorted_peaks = sorted([p for p in peaks if p < duration])
    
    current_t = 0.0
    for p in sorted_peaks:
        # Before peak (sustain base)
        if p > current_t + 0.1:
            keyframes.append(Keyframe(p - 0.1, base_val * 0.5))
        
        # Peak
        accent = base_val * rng.uniform(1.2, 1.8)
        keyframes.append(Keyframe(p, min(1.0, accent)))
        
        # Recovery
        keyframes.append(Keyframe(p + 0.2, base_val * 0.5))
        current_t = p + 0.2
        
    keyframes.append(Keyframe(duration, 0.0))
    return keyframes

def vcd_escape(s: str) -> str:
    return s.replace('"', '\\"')

def format_keyframe_combo(name: str, keyframes: List[Keyframe], indent="        ") -> str:
    lines = [f'{indent}"{name}"         combo        ', f"{indent}{{"]
    for kf in keyframes: lines.append(f"{indent}  {kf.time:.4f} {kf.value:.4f}")
    lines.append(f"{indent}}}")
    lines.append(f"{indent}{{")
    for kf in keyframes: lines.append(f"{indent}  {kf.time:.4f} 0.5000")
    lines.append(f"{indent}}}")
    return "\n".join(lines)

def format_keyframe_range(name: str, keyframes: List[Keyframe], rmax, rmin, indent="        ") -> str:
    lines = [f'{indent}"{name}"         range {rmax:.1f} {rmin:.1f}        ', f"{indent}{{"]
    for kf in keyframes: lines.append(f"{indent}  {kf.time:.4f} {kf.value:.4f}")
    lines.append(f"{indent}}}")
    return "\n".join(lines)

def format_keyframe_simple(name: str, keyframes: List[Keyframe], indent="        ") -> str:
    lines = [f'{indent}"{name}"        ', f"{indent}{{"]
    for kf in keyframes: lines.append(f"{indent}  {kf.time:.4f} {kf.value:.4f}")
    lines.append(f"{indent}}}")
    return "\n".join(lines)

def build_flexanimation_block(node: DialogNode, emotion: str, rng: random.Random, scene_dir: Path) -> str:
    curves = {}
    
    # 1. Embed VDAT (Lipsync)
    embed_vdat_data(node, node.duration, rng, scene_dir)
    
    # 1.5 Scan Audio Accents
    accents = scan_audio_peaks(node.wav_path)
    
    # 2. Emotion (With Asymmetry & Accents)
    profile = EMOTION_PROFILES.get(emotion, EMOTION_PROFILES["neutral"])
    for name, base_val in profile.items():
        val = base_val * rng.uniform(0.8, 1.2)
        
        # ASYMMETRY logic
        if name in ["inner_raiser", "outer_raiser", "lid_tightener"]:
            l_val = val * rng.uniform(0.8, 1.2)
            r_val = val * rng.uniform(0.8, 1.2)
            
            curves[f"left_{name}"] = ("combo", generate_accented_curve(node.duration, accents, l_val, rng) if accents and val > 0.1 else generate_smooth_curve(node.duration, l_val, rng=rng), None)
            curves[f"right_{name}"] = ("combo", generate_accented_curve(node.duration, accents, r_val, rng) if accents and val > 0.1 else generate_smooth_curve(node.duration, r_val, rng=rng), None)
            curves[name] = ("combo", generate_smooth_curve(node.duration, val, rng=rng), None)
        elif val > 0.01:
            curves[name] = ("combo", generate_smooth_curve(node.duration, val, rng=rng), None)
            
    # 2.5 Micro Expressions
    bg_flexes = ["dilator", "cheek_raiser", "compressor", "corner_puller", "wrinkler"]
    for flex in bg_flexes:
        if flex not in curves and rng.random() < 0.6: 
             intensity = rng.uniform(0.05, 0.15)
             curves[f"left_{flex}"] = ("combo", generate_background_noise(node.duration, intensity * rng.uniform(0.5, 1.0), rng=rng), None)
             curves[f"right_{flex}"] = ("combo", generate_background_noise(node.duration, intensity * rng.uniform(0.5, 1.0), rng=rng), None)
             curves[flex] = ("combo", generate_background_noise(node.duration, intensity, rng=rng), None)

    # 3. Blink
    if kfs := generate_blink_keyframes(node.duration, rng):
        curves["left_lid_closer"] = ("combo", kfs, None)
        curves["right_lid_closer"] = ("combo", [Keyframe(k.time, k.value * rng.uniform(0.9, 1.1)) for k in kfs], None)
        curves["lid_closer"] = ("combo", kfs, None)
    curves["eyes_rightleft"] = ("range", generate_eye_movement(node.duration, rng, "rightleft"), (30.0, -30.0))
    curves["eyes_updown"] = ("range", generate_eye_movement(node.duration, rng, "updown"), (30.0, -30.0))
    
    # 4. Head (With Accents)
    curves["head_rightleft"] = ("range", generate_head_movement(node.duration, emotion, rng, "rightleft"), (30.0, -30.0))
    curves["head_updown"] = ("range", generate_head_movement(node.duration, emotion, rng, "updown"), (15.0, -15.0))
    curves["head_tilt"] = ("range", generate_head_movement(node.duration, emotion, rng, "tilt"), (15.0, -15.0))
    
    # Nods on accents (NERFED)
    if accents:
        # Only strongest peaks, larger distance, lower intensity
        curves["head_forwardback"] = ("range", generate_accented_curve(node.duration, accents, 0.15, rng), (5.0, -5.0))
    
    blocks = []
    for name, (ctype, kfs, rvals) in curves.items():
        if ctype == "combo": blocks.append(format_keyframe_combo(name, kfs))
        elif ctype == "range": blocks.append(format_keyframe_range(name, kfs, rvals[0], rvals[1]))
        else: blocks.append(format_keyframe_simple(name, kfs))
    return "\n".join(blocks)

def write_vcd(cfg: Config, node: DialogNode, out_path: Path) -> None:
    emotion = detect_emotion(node.text)
    rng = random.Random(hash(node.node_id) & 0xFFFFFFFF)
    
    flex_block = build_flexanimation_block(node, emotion, rng, cfg.scene_dir)
    sound_token = node.sound_rel.replace("/", "\\")
    
    # Gestures
    gesture = get_context_aware_gesture(node.text, emotion, node.duration, rng)
    gesture_block = ""
    if gesture and node.duration > 1.0: 
        gesture_block = f"""  channel "Gesture"
  {{
    event gesture "{gesture}"
    {{
      time 0.000000 {node.duration:.6f}
      param "{gesture}"
      sequenceduration {node.duration:.6f}
    }}
  }}"""

    lines = [
        "// Choreo version 1",
        f'actor "{vcd_escape(cfg.actor)}"',
        "{",
        '  channel "audio"', "  {",
        f'    event speak "{vcd_escape(sound_token)}"', "    {",
        f"      time 0.000000 {node.duration:.6f}",
        f'      param "{vcd_escape(sound_token)}"',
        "      fixedlength", '      cctype "cc_master"', '      cctoken ""',
        "    }", "  }",
        '  channel "look at"', "  {",
        '    event lookat "Look at player"', "    {",
        f"      time 0.000000 {node.duration:.6f}",
        '      param "!player"',
        "    }", "  }",
        '  channel "face to"', "  {",
        '    event face "Face player"', "    {",
        f"      time 0.000000 {node.duration:.6f}",
        '      param "!player"',
        "    }", "  }",
        '  channel "facial"', "  {",
        '    event flexanimation "facial"', "    {",
        f"      time 0.000000 {node.duration:.6f}",
        '      param ""',
        "      flexanimations samples_use_time", "      {",
        flex_block,
        "      }", "    }", "  }",
        gesture_block,
        "}", "",
        "scalesettings", "{",
        '  "CChoreoView" "100"', '  "ExpressionTool" "100"', '  "RampTool" "100"',
        '  "GestureTool" "100"', '  "SceneRampTool" "100"',
        "}", "fps 60", "snap off", "ignorePhonemes off", ""
    ]
    
    out_path.parent.mkdir(parents=True, exist_ok=True)
    if out_path.exists() and not cfg.overwrite:
        print(f"[SKIP] {out_path} exists")
        return
    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"[OK] {node.node_id}: {out_path.name} ({emotion}, gesture={gesture})")

# ============================================================================
# LIPSYNC GENERATION (PRO)
# ============================================================================

@dataclass
class WordTiming:
    text: str
    phonemes: List[str]
    start: float
    end: float
    ph_timings: List[Tuple[str, float, float]] # (code, start, end)

def calculate_smart_timing(text: str, duration: float) -> List[WordTiming]:
    """
    Distributes phonemes intelligently:
    - Inserts silence at punctuation.
    - Centers speech if WAV is much longer than text.
    - Avoids over-stretching.
    """
    # 1. Tokenize (Words + Punctuation)
    # Simple split by space, then check punctuation
    raw_tokens = text.split()
    tokens = []
    
    # Analyze text for pauses
    # Heuristic: ',' = 0.3s, '.' = 0.6s, '...' = 0.8s
    base_pause = 0.05
    comma_pause = 0.25
    period_pause = 0.5
    
    items = [] # (type, content, duration) : type=WORD/PAUSE
    
    for t in raw_tokens:
        clean = re.sub(r"[^a-zA-Z']", "", t).lower()
        if not clean: continue
        
        phs = text_to_phonemes(clean)
        # Base duration for word
        dur = sum(get_phoneme_duration(p) for p in phs)
        items.append({"type": "WORD", "text": t, "phonemes": phs, "base_dur": dur})
        
        # Check trailing punctuation
        if t.endswith("..."): items.append({"type": "PAUSE", "dur": period_pause * 1.5})
        elif t.endswith("."): items.append({"type": "PAUSE", "dur": period_pause})
        elif t.endswith("?"): items.append({"type": "PAUSE", "dur": period_pause})
        elif t.endswith("!"): items.append({"type": "PAUSE", "dur": period_pause})
        elif t.endswith(","): items.append({"type": "PAUSE", "dur": comma_pause})
        else: items.append({"type": "PAUSE", "dur": base_pause}) # Gap between words
        
    # Total Base Time
    total_base_speech = sum(i["base_dur"] for i in items if i["type"] == "WORD")
    total_base_pause = sum(i["dur"] for i in items if i["type"] == "PAUSE")
    total_req = total_base_speech + total_base_pause
    
    # Calculate Stretch Factors
    # We want to fill 'duration', but not if it's mostly silence at end.
    
    # Case 1: Audio is shorter than required text (Fast speech)
    # Compress everything to fit.
    if duration < total_req:
        scale = duration / total_req
        word_scale = scale
        pause_scale = scale
        start_t = 0.0
    
    # Case 2: Audio is longer (Normal/Slow speech or Padding)
    else:
        slack = duration - total_req
        
        # If slack is huge (e.g. 5s silence), don't stretch speech excessively.
        # Max natural stretch is approx 1.2x
        # We put strict limit on speech stretching.
        
        max_speech_stretch = 1.3
        
        # Calculate how much slack speech CAN define
        allowed_speech_slack = (total_base_speech * max_speech_stretch) - total_base_speech
        
        # Does the slack fit in speech stretch?
        if slack <= allowed_speech_slack:
            # Mild stretch distributed everywhere
            scale = duration / total_req
            word_scale = scale
            pause_scale = scale
            start_t = 0.0
        else:
            # Slack is overflowing.
            # Stretch speech to max.
            word_scale = max_speech_stretch
            
            # Remaining slack goes to pauses and padding
            used_by_speech = total_base_speech * word_scale
            remaining_for_gaps = duration - used_by_speech
            
            # Distribute remaining: 20% start padding, 20% end padding, 60% pauses
            # OR just inflate pauses evenly
            # Let's add explicit Padding items
            pad_time = remaining_for_gaps * 0.2 # 10% start, 10% end
            pause_pool = remaining_for_gaps - pad_time
            
            pause_scale = pause_pool / total_base_pause if total_base_pause > 0 else 1.0
            start_t = pad_time * 0.5
            
    # Generate Timings
    result = []
    curr_t = start_t
    
    for item in items:
        if item["type"] == "PAUSE":
            curr_t += item["dur"] * pause_scale
        elif item["type"] == "WORD":
            w_dur = item["base_dur"] * word_scale
            w_start = curr_t
            w_end = w_start + w_dur
            
            # Distribute phonemes within word
            phs = item["phonemes"]
            base_ph_sum = sum(get_phoneme_duration(p) for p in phs)
            ph_local_scale = w_dur / base_ph_sum if base_ph_sum > 0 else 1.0
            
            ph_timings = []
            p_t = w_start
            for p in phs:
                pd = get_phoneme_duration(p) * ph_local_scale
                ph_timings.append((p, p_t, p_t + pd))
                p_t += pd
                
            result.append(WordTiming(item["text"], phs, w_start, w_end, ph_timings))
            curr_t = w_end
            
    return result

def create_faceposer_data_smart(text: str, duration: float, timings: List[WordTiming]) -> str:
    """Generate VDAT chunk with smart timing."""
    nl = "\r\n"
    lines = []
    lines.extend(["VERSION 1.0", "PLAINTEXT", "{", f'"{text}"', "}", "WORDS", "{"])
    
    # Phoneme ID Helper
    PHONEME_IDS = {
        'sil': 95, 'aa': 593, 'ae': 230, 'ah': 652, 'ao': 596, 'aw': 593, 'ay': 593,
        'b': 98, 'ch': 679, 'd': 100, 'dh': 240, 'eh': 603, 'er': 602, 'ey': 101,
        'f': 102, 'g': 103, 'hh': 104, 'ih': 618, 'iy': 105, 'jh': 676, 'k': 107,
        'l': 108, 'm': 109, 'n': 110, 'ng': 331, 'ow': 111, 'p': 112, 'r': 633,
        's': 115, 'sh': 643, 't': 116, 'th': 952, 'uh': 650, 'uw': 117, 'v': 118,
        'w': 119, 'y': 106, 'z': 122, 'zh': 658
    }
    
    def get_id(ph_code):
        code = re.sub(r"[012]", "", ph_code).lower()
        return PHONEME_IDS.get(code, 95)

    for w in timings:
        lines.append(f'WORD {w.text} {w.start:.6f} {w.end:.6f}')
        lines.append("{")
        for code, start, end in w.ph_timings:
            clean_p = re.sub(r"[012]", "", code).lower()
            pid = get_id(code)
            lines.append(f'{pid} {clean_p} {start:.6f} {end:.6f} 1')
        lines.append("}")
        
    lines.extend(["}", "EMPHASIS", "{", "}", "CLOSECAPTION", "{", "}", "OPTIONS", "{", "voice_duck 0", "}"])
    return nl.join(lines)

def read_wav_data_robust(wav_path: Path) -> Tuple[int, int, bytes]:
    """
    Manually parses RIFF WAV to find fmt and data chunks.
    Returns (framerate, n_channels, raw_bytes_16bit).
    """
    with open(wav_path, "rb") as f:
        # Check RIFF
        if f.read(4) != b"RIFF": return 0, 0, b""
        f.read(4) # size
        if f.read(4) != b"WAVE": return 0, 0, b""
        
        framerate = 0
        n_channels = 0
        audio_data = b""
        
        while True:
            chunk_id = f.read(4)
            if len(chunk_id) < 4: break
            size_bytes = f.read(4)
            if len(size_bytes) < 4: break
            chunk_size = struct.unpack("<I", size_bytes)[0]
            
            if chunk_id == b"fmt ":
                fmt_data = f.read(chunk_size)
                audio_fmt, n_channels, framerate = struct.unpack("<HHI", fmt_data[:8])
                if audio_fmt != 1: # Not PCM
                     return 0, 0, b""
            elif chunk_id == b"data":
                audio_data = f.read(chunk_size)
                break # Stop after data
            else:
                f.seek(chunk_size, 1) # Skip unknown
                
        return framerate, n_channels, audio_data

def scan_audio_silence(wav_path: Path, threshold_db: float = -35.0) -> Tuple[float, float]:
    """Robust silence detection."""
    try:
        framerate, n_channels, raw_data = read_wav_data_robust(wav_path)
        if not raw_data:
             # Fallback to wave module
             return scan_audio_silence_legacy(wav_path, threshold_db)
             
        # 16-bit signed
        total_samples = len(raw_data) // 2
        fmt = f"<{total_samples}h"
        samples = struct.unpack(fmt, raw_data)
        
        threshold_amp = 32768 * (10 ** (threshold_db / 20.0))
        chunk_size = int(framerate * 0.01) # 10ms
        
        start_t = 0.0
        end_t = total_samples / n_channels / framerate
        
        # Forward
        for i in range(0, len(samples), chunk_size * n_channels):
            chunk = samples[i : i + chunk_size * n_channels]
            if not chunk: break
            rms = math.sqrt(sum(s*s for s in chunk) / len(chunk))
            if rms > threshold_amp:
                start_t = (i // n_channels) / framerate
                break
                
        # Backward
        for i in range(len(samples) - (chunk_size*n_channels), 0, -(chunk_size*n_channels)):
            chunk = samples[i : i + chunk_size * n_channels]
            if not chunk: continue
            rms = math.sqrt(sum(s*s for s in chunk) / len(chunk))
            if rms > threshold_amp:
                end_t = ((i // n_channels) + chunk_size) / framerate
                break
                
        # Safety padding
        start_t = max(0.0, start_t - 0.05)
        end_t = min(end_t + 0.05, total_samples / n_channels / framerate)
        
        with open("audio_scan.log", "a") as log:
            log.write(f"Scanning {wav_path}: fr={framerate}, ch={n_channels}, len={len(raw_data)}\n")
            log.write(f"Detected: start={start_t:.3f}, end={end_t:.3f}\n")
            
        return start_t, end_t
        
    except Exception as e:
        with open("audio_scan.log", "a") as log:
            log.write(f"SCAN FAILED {wav_path}: {e}\n")
        return 0.0, 0.0 # Will fallback

def scan_audio_silence_legacy(wav_path: Path, threshold_db: float = -35.0) -> Tuple[float, float]:
    """Fallback using wave module."""
    try:
        with wave.open(str(wav_path), "rb") as wf:
             # (Existing implementation logic...)
             n_frames = wf.getnframes()
             fr = wf.getframerate()
             return 0.0, n_frames / fr 
    except: return 0.0, 0.0

def scan_audio_peaks(wav_path: Path, min_dist=0.8, threshold_ratio=0.7) -> List[float]:
    """Accents detection using robust reader. Nerfed for head nods."""
    peaks = []
    try:
        fr, nc, raw = read_wav_data_robust(wav_path)
        if not raw: return []
        
        samples = struct.unpack(f"<{len(raw)//2}h", raw)
        chunk = int(fr * 0.05)
        envelope = []
        
        for i in range(0, len(samples), chunk * nc):
            s_chunk = samples[i : i + chunk * nc]
            if not s_chunk: continue
            rms = math.sqrt(sum(s*s for s in s_chunk) / len(s_chunk))
            envelope.append((i / nc / fr, rms))
            
        max_amp = max(e[1] for e in envelope) if envelope else 0
        threshold = max_amp * threshold_ratio
        
        last_peak = -1.0
        for t, amp in envelope:
            if amp > threshold:
                if last_peak < 0 or (t - last_peak) > min_dist:
                    peaks.append(t)
                    last_peak = t
    except: pass
    return peaks

def scan_audio_segments(wav_path: Path, threshold_db: float = -28.0, min_gap=0.1) -> List[Tuple[float, float, float]]:
    """Detects 'islands' of audio energy. High sensitivity for discrete phrases."""
    segments = []
    try:
        fr, nc, raw = read_wav_data_robust(wav_path)
        if not raw: return []
        
        samples = struct.unpack(f"<{len(raw)//2}h", raw)
        threshold_amp = 32768 * (10 ** (threshold_db / 20.0))
        chunk_size = int(fr * 0.015)
        
        in_segment = False
        seg_start = 0.0
        peak_energy = 0.0
        peak_t = 0.0
        
        for i in range(0, len(samples), chunk_size * nc):
            chunk = samples[i : i + chunk_size * nc]
            if not chunk: break
            rms = math.sqrt(sum(s*s for s in chunk) / len(chunk))
            t = (i / nc) / fr
            
            if rms > threshold_amp:
                if not in_segment:
                    seg_start = max(0, t - 0.02)
                    in_segment = True
                    peak_energy = rms; peak_t = t
                else:
                    if rms > peak_energy: peak_energy = rms; peak_t = t
            else:
                if in_segment:
                    # Look ahead for a significant silence gap
                    lookahead_s = int(fr * min_gap)
                    has_more = False
                    if i + chunk_size*nc + lookahead_s*nc < len(samples):
                        next_chunk = samples[i+chunk_size : i+chunk_size + lookahead_s*nc]
                        # Use slightly higher threshold for lookahead to avoid re-triggering on noise
                        if next_chunk and math.sqrt(sum(s*s for s in next_chunk)/len(next_chunk)) > threshold_amp * 1.1:
                            has_more = True
                    
                    if not has_more:
                        segments.append((seg_start, t + 0.02, peak_t))
                        in_segment = False
        
        if in_segment: segments.append((seg_start, len(samples)/nc/fr, peak_t))
        
        # Merge segments within 200ms (typical intra-sentence/phrase gap)
        if len(segments) > 1:
            merged = [segments[0]]
            for next_s in segments[1:]:
                prev_s = merged[-1]
                if next_s[0] - prev_s[1] < 0.2:
                    merged[-1] = (prev_s[0], next_s[1], max(prev_s[2], next_s[2]))
                else:
                    merged.append(next_s)
            segments = merged

        with open("segment_debug.log", "a") as f:
            f.write(f"WAV: {wav_path} (Detected {len(segments)} segments)\n")
            for s in segments: f.write(f"  SEG: {s[0]:.3f} - {s[1]:.3f}\n")
    except Exception as e:
        with open("segment_debug.log", "a") as f:
             f.write(f"ERROR {wav_path}: {e}\n")
    return segments

def embed_vdat_data(node: DialogNode, duration: float, rng: random.Random, scene_dir: Path) -> None:
    """Generates and embeds VDAT using Full-Burst Word Distribution."""
    try:
        segments = scan_audio_segments(node.wav_path)
        
        # 1. Break text into phrase groups by punctuation
        # This matches the SAPI logic of treating "Real nasty," as a block.
        phrase_groups = []
        raw = re.split(r"([,.!?;])", node.text)
        curr = ""
        for p in raw:
            if re.match(r"[,.!?;]", p): 
                phrase_groups.append(curr + p); curr = ""
            else:
                if curr: curr += p
                else: curr = p
        if curr.strip(): phrase_groups.append(curr)

        # 2. Extract word details
        phrase_data = []
        for p_text in phrase_groups:
            p_words = p_text.split()
            w_list = []
            p_base_dur = 0
            for w in p_words:
                clean = re.sub(r"[^a-zA-Z']", "", w).lower()
                phs = text_to_phonemes(clean)
                wb_dur = sum(get_phoneme_duration(ph) for ph in phs)
                w_list.append({"text": w, "phs": phs, "base_dur": wb_dur})
                p_base_dur += wb_dur
            phrase_data.append({"words": w_list, "base_dur": p_base_dur})

        timings = []
        if not segments:
            timings = calculate_smart_timing(node.text, duration)
        else:
            # 1. Extract ALL words
            all_words = []
            for w in node.text.split():
                clean = re.sub(r"[^a-zA-Z']", "", w).lower()
                phs = text_to_phonemes(clean)
                bd = sum(get_phoneme_duration(p) for p in phs)
                all_words.append({"text": w, "phs": phs, "base_dur": bd})
            
            w_idx = 0
            num_segs = len(segments)
            total_seg_dur = sum(s[1]-s[0] for s in segments)
            
            for i, seg in enumerate(segments):
                s_start, s_end, _ = seg
                s_dur = s_end - s_start
                
                # 2. Balanced Grouping: Assign a count of words to this island
                # based on its proportion of the total audio duration.
                share = s_dur / sum(s[1]-s[0] for s in segments[i:]) if i < num_segs else 1.0
                num_remaining = len(all_words) - w_idx
                count = max(1, round(num_remaining * share))
                if i == num_segs - 1: count = num_remaining
                
                seg_words = all_words[w_idx : w_idx + count]
                w_idx += count
                
                if not seg_words: continue
                
                # 3. Fill the island
                seg_base = sum(sw["base_dur"] for sw in seg_words)
                seg_scale = s_dur / seg_base if seg_base > 0 else 1.0
                
                curr_t = s_start
                for sw in seg_words:
                    wdur = sw["base_dur"] * seg_scale
                    wend = curr_t + wdur
                    ph_ts = []
                    pt = curr_t
                    for ph in sw["phs"]:
                        phd = get_phoneme_duration(ph) * seg_scale
                        ph_ts.append((ph, pt, pt + phd))
                        pt += phd
                    timings.append(WordTiming(sw["text"], sw["phs"], curr_t, wend, ph_ts))
                    curr_t = wend
                    
        data_str = create_faceposer_data_smart(node.text, duration, timings)
        embed_wav_phonemes(node.wav_path, data_str)
    except Exception as e:
        print(f"[WARN] Embedding failed for {node.node_id}: {e}")

# ============================================================================
# PARSING & MAIN
# ============================================================================

class KVTokenizer:
    def __init__(self, text: str):
        self.text = text
        self.pos = 0
        self.length = len(text)
    def skip(self):
        while self.pos < self.length:
            c = self.text[self.pos]
            if c in " \t\r\n": self.pos += 1
            elif c == "/" and self.pos + 1 < self.length:
                if self.text[self.pos+1] == "/":
                    while self.pos < self.length and self.text[self.pos] != "\n": self.pos += 1
                else: break
            else: break
    def next_token(self):
        self.skip()
        if self.pos >= self.length: return None
        c = self.text[self.pos]
        if c in "{}": self.pos += 1; return c
        if c == '"':
            self.pos += 1; start = self.pos
            while self.pos < self.length and self.text[self.pos] != '"':
                if self.text[self.pos] == "\\" and self.pos+1 < self.length: self.pos += 2
                else: self.pos += 1
            res = self.text[start:self.pos]; self.pos += 1; return res
        start = self.pos
        while self.pos < self.length and self.text[self.pos] not in ' \t\r\n{}"\\/': self.pos += 1
        return self.text[start:self.pos]
    def _parse_obj(self):
        res = {}
        while True:
            t = self.next_token()
            if t is None or t == "}": break
            if (val := self.next_token()) == "{": res[t] = self._parse_obj()
            else: res[t] = val
        return res
    def parse(self): 
        k = self.next_token()
        if self.next_token() != "{": raise ValueError()
        return {k: self._parse_obj()}

def parse_keyvalues(text: str) -> Dict:
    return KVTokenizer(text).parse()

def get_wav_duration(path: Path) -> float:
    try:
        with wave.open(str(path), "rb") as wf: return wf.getnframes() / wf.getframerate()
    except: return 3.0

def parse_dialog_file(cfg: Config) -> Tuple[List[DialogNode], Optional[DialogNode]]:
    data = parse_keyvalues(cfg.dialog_path.read_text(encoding="utf-8"))
    nodes = []
    returning = None
    
    for nid, ndata in data.get("Dialog", {}).get("nodes", {}).items():
        if not isinstance(ndata, dict): continue
        if not (line := ndata.get("line")) or not (choreo := ndata.get("choreo")): continue
        wav_path = cfg.sound_dir / f"{nid}.wav"
        if not wav_path.exists(): 
            print(f"[WARN] Missing {wav_path}"); continue
        nodes.append(DialogNode(nid, line, choreo, wav_path, get_wav_duration(wav_path), f"{cfg.actor}/{nid}.wav"))
        
    rline = data.get("Dialog", {}).get("returning_line")
    rchoreo = data.get("Dialog", {}).get("returning_choreo")
    if rline and rchoreo:
        for wname in ["returningline.wav", "returning.wav"]:
            if (wp := cfg.sound_dir / wname).exists():
                returning = DialogNode("returning", rline, rchoreo, wp, get_wav_duration(wp), f"{cfg.actor}/{wname}")
                break
    return nodes, returning

def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sound-dir", type=Path, default=DEFAULT_SOUNDDIR)
    parser.add_argument("--scene-dir", type=Path, default=DEFAULT_SCENEDIR)
    parser.add_argument("--dialog", type=Path, default=DEFAULT_DIALOGPATH)
    parser.add_argument("--actor", default=DEFAULT_ACTOR)
    args = parser.parse_args(argv)
    
    cfg = Config(
        sound_dir=args.sound_dir,
        scene_dir=args.scene_dir,
        dialog_path=args.dialog,
        actor=args.actor
    )
    
    if not cfg.sound_dir.exists(): return 1
    nodes, ret = parse_dialog_file(cfg)
    cfg.scene_dir.mkdir(parents=True, exist_ok=True)
    
    for n in nodes: write_vcd(cfg, n, cfg.scene_dir / f"{n.node_id}.vcd")
    if ret: write_vcd(cfg, ret, cfg.scene_dir / "returning.vcd")
    
    print(f"[DONE] Generated {len(nodes) + (1 if ret else 0)} VCDs")
    return 0

if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))