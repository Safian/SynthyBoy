#!/usr/bin/env python3
"""Publish one current screen image; historical references are never rolling backups."""
import argparse
from pathlib import Path
from PIL import Image
from screen_images import screen_path, save_screen


def publish(source, directory, screen, state='default'):
    if state != 'default':
        raise ValueError('Only the canonical default state can be published; export variants outside the repository')
    target = screen_path(screen)
    if Path(directory).resolve() != target.parent:
        raise ValueError('Screen images have one flat directory: docs/screens')
    with Image.open(source) as image:
        return save_screen(image, target)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('screen')
    parser.add_argument('--state', default='default', choices=['default'])
    args = parser.parse_args()
    print(publish(args.source, screen_path(args.screen).parent, args.screen, args.state))
