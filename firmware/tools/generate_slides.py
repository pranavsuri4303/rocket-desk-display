"""Validate the slideshow config and emit a header for firmware and host tests."""

import hashlib
import json
import re
import sys
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def generate(config):
    require(isinstance(config, dict) and set(config) == {"slide_seconds", "slides"},
            "expected slide_seconds and slides")
    seconds = config["slide_seconds"]
    require(type(seconds) is int and 5 <= seconds <= 60, "slide_seconds must be 5–60")
    slides = config["slides"]
    require(isinstance(slides, list) and 1 <= len(slides) <= 8, "expected 1–8 slides")
    routes, features, stops, ids = [], [], [], set()
    for index, slide in enumerate(slides, 1):
        prefix = f"slide {index}"
        require(isinstance(slide, dict), f"{prefix}: expected an object")
        kind = slide.get("type")
        if kind == "clock":
            require(set(slide) == {"type"}, f"{prefix}: clock accepts only type")
            slide_id = "clock"
            feature = 'X("clock", "Clock", render_clock, NULL)'
        else:
            require(kind == "bus", f"{prefix}: type must be bus or clock")
            fields = ("id", "stop_code", "line", "stop", "direction")
            require(set(slide) == {"type", *fields}, f"{prefix}: missing or unknown bus fields")
            require(all(isinstance(slide[k], str) for k in fields),
                    f"{prefix}: bus fields must be strings")
            slide_id, stop_code, line, stop, direction = (slide[k] for k in fields)
            require(re.fullmatch(r"[a-z0-9][a-z0-9-]{0,31}", slide_id),
                    f"{prefix}: id must be 1–32 lowercase letters, digits or hyphens")
            require(re.fullmatch(r"[0-9]{1,12}", stop_code), f"{prefix}: invalid stop_code")
            require(re.fullmatch(r"[A-Za-z0-9]{1,3}", line), f"{prefix}: invalid bus line")
            for label in (stop, direction):
                require(re.fullmatch(r"[A-Za-z0-9 /.+:-]{1,20}", label) and label.strip(),
                        f"{prefix}: stop and direction must be 1–20 display characters")
            args = ", ".join(json.dumps(slide[k]) for k in fields)
            routes.append(f"X({args})")
            label = json.dumps(f"{line} to {direction}")
            feature = f'X("{slide_id}", {label}, render_bus, &glance_routes[{len(routes)-1}])'
            if stop_code not in stops:
                stops.append(stop_code)
        require(slide_id not in ids, f"{prefix}: duplicate id {slide_id}")
        ids.add(slide_id)
        features.append(feature)

    url = f"http://v0.ovapi.nl/tpc/{','.join(stops)}/departures" if stops else ""
    digest = hashlib.sha256(json.dumps(config, sort_keys=True).encode()).hexdigest()[:16]
    continuation = " " + chr(92) + "\n    "
    return "\n".join((
        "/* Generated from config/slides.json. */",
        "#pragma once",
        f"#define GLANCE_CONFIG_ID \"{digest}\"",
        f"#define GLANCE_SLIDE_SECONDS {seconds}",
        f"#define GLANCE_ROUTE_COUNT {len(routes)}",
        f"#define GLANCE_TRANSIT_URL {json.dumps(url)}",
        "#define GLANCE_CONFIG_ROUTES(X) " + continuation.join(routes),
        "#define GLANCE_CONFIG_FEATURES(X) " + continuation.join(features),
        "",
    ))


if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    try:
        result = generate(json.loads(source.read_text()))
        if not target.exists() or target.read_text() != result:
            target.write_text(result)
    except (OSError, ValueError) as error:
        sys.exit(f"{source}: {error}")
