#!/usr/bin/env python

import difflib
import re
import string
import subprocess
import StringIO
import sys
import os
import argparse


def get_changes():
    strip_prefix = 1
    filename = None
    lines_by_file = dict()

    for line in sys.stdin:
        match = re.search('^\+\+\+\ (.*?)/{%s}(\S*)' % strip_prefix, line)
        if match:
            filename = match.group(2)
        if filename is None:
            continue

        if not re.match('^%s$' % r'.*\.(cpp|cc|c\+\+|cxx|c|h|hpp)', filename, re.IGNORECASE):
            continue

        match = re.search('^@@.*\+(\d+)(,(\d+))?', line)
        if match:
            lines_by_file.setdefault(filename, [])
            start_line = int(match.group(1))
            line_count = 1
            if match.group(3):
                line_count = int(match.group(3))
            if line_count == 0:
                continue
            end_line = start_line + line_count - 1
            lines_by_file[filename].append('-lines={}:{}'.format(
                str(start_line),
                str(end_line)
            ))

    return lines_by_file


def get_style(filename, styles):
    filename = filename.strip()
    if filename in styles['files']:
        return styles['files'][filename]
    elif os.path.dirname(filename) in styles['dirs']:
        return styles['dirs'][os.path.dirname(filename)]
    else:
        return None


def view_changes(binary, files_by_style):
    for filename, lines in files_by_style.iteritems():
        command = list()
        command.append(binary)
        command.append('-style=' + lines['style'])
        command.extend(lines['lines_changed'])
        command.append(filename)
        pipe = subprocess.Popen(command, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, stdin=None)
        stdout, stderr = pipe.communicate()
        if pipe.returncode != 0:
            print stderr
            sys.exit(pipe.returncode)

        with open(filename) as fname:
            code = fname.readlines()
        formatted_code = StringIO.StringIO(stdout).readlines()
        diff = difflib.unified_diff(code, formatted_code, filename, filename,
                                    '(before formatting)', '(after formatting)')
        diff_string = string.join(diff, '')
        str_len = len(diff_string)
        if str_len > 0:
            sys.stdout.write(diff_string)


def format_changes(binary, files_by_style):
    for filename, lines in files_by_style.iteritems():
        command = list()
        command.append(binary)
        command.append('-style=' + lines['style'])
        command.append('-i')
        command.extend(lines['lines_changed'])
        command.append(filename)
        pipe = subprocess.Popen(command, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, stdin=None)
        stdout, stderr = pipe.communicate()
        if pipe.returncode != 0:
            print stderr
            sys.exit(pipe.returncode)


def read_styles(lines):
    styles = {
        "files": dict(),
        "dirs": dict()
    }
    for line in lines:
        path = line.split(' ', 1)[1].strip()
        style = line.split(' ', 1)[0].strip()
        if os.path.isdir(path):
            styles['dirs'].update({
                path: style
            })
        else:
            styles['files'].update({
                path: style
            })
    return styles


def main():
    parser = argparse.ArgumentParser(description=
                                     'Reformat changed lines in diff.')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='be more verbose')
    parser.add_argument('-p', '--path', action='store', default='',
                        help='path to folder or file')
    parser.add_argument('-binary', default='clang-format',
                        help='location of binary to use for clang-format')
    args = parser.parse_args()

    pwd = os.path.dirname(__file__)
    os.chdir(pwd)

    styles_file = "STYLES"
    if not os.path.isfile(os.path.join(pwd, styles_file)):
        print "STYLES does not exist."
        sys.exit(1)

    styles_file = os.path.join(pwd, styles_file)
    with open(styles_file) as fname:
        lines = fname.readlines()

    styles = read_styles(lines)
    lines_by_file = get_changes()

    files_by_style = dict()
    for filename in lines_by_file:
        files_by_style.update(
            {
                filename: {
                    "style": get_style(filename, styles),
                    "lines_changed": lines_by_file[filename]
                }
            }
        )
        if files_by_style[filename]['style'] is None:
            files_by_style[filename]['style'] = 'xen'

    if args.verbose:
        print "Following changes are to be performed:\n"
        view_changes(args.binary, files_by_style)
    format_changes(args.binary, files_by_style)

if __name__ == '__main__':
    main()
