#!/usr/bin/env python

import difflib
import re
import string
import subprocess
import StringIO
import sys
import os


def get_changes(diff_file):
    with open(diff_file) as f:
        lines = f.readlines()
    strip_prefix = 1
    filename = None
    lines_by_file = {}
    for line in lines:
        match = re.search('^\+\+\+\ (.*?)/{%s}(\S*)' % strip_prefix, line)
        if match:
            filename = match.group(2)
        if filename is None:
            continue

        if not re.match('^%s$' % r'.*\.(cpp|cc|c\+\+|cxx|c|h|hpp)', filename, re.IGNORECASE):
            continue

        match = re.search('^@@.*\+(\d+)(,(\d+))?', line)
        if match:
            start_line = int(match.group(1))
            line_count = 1
            if match.group(3):
                line_count = int(match.group(3))
            if line_count == 0:
                continue
            end_line = start_line + line_count - 1
            lines_by_file.setdefault(filename, []).extend(['-lines', str(start_line) + ':' + str(end_line)])
    return lines_by_file


def view_changes(binary, lines_by_file):
    for filename, lines in lines_by_file.iteritems():
        command = [binary, '-style=file']
        command.extend(lines)
        command.append(filename)
        p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=None)
        stdout, stderr = p.communicate()
        if p.returncode != 0:
            sys.exit(p.returncode)

        with open(filename) as f:
            code = f.readlines()
        formatted_code = StringIO.StringIO(stdout).readlines()
        diff = difflib.unified_diff(code, formatted_code, filename, filename,
                                    '(before formatting)', '(after formatting)')
        diff_string = string.join(diff, '')
        if len(diff_string) > 0:
            sys.stdout.write(diff_string)


def format_changes(binary, lines_by_file):
    for filename, lines in lines_by_file.iteritems():
        command = [binary, '-style=file']
        command.append('-i')
        command.extend(lines)
        command.append(filename)
        p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=None)
        p.communicate()
        if p.returncode != 0:
            sys.exit(p.returncode)


def main():
    pwd = os.path.dirname(__file__)
    diff_file = "diff.txt"
    if not os.path.isfile(os.path.join(pwd, diff_file)):
        print "diff.txt does not exist. Exiting..."
        sys.exit(1)
    diff_file = os.path.join(pwd, diff_file)
    binary = 'clang-format-3.9'
    lines_by_file = get_changes(diff_file)
    if lines_by_file:
        print "Changed file(s):"
        for files in lines_by_file.keys():
            print files
        # print "Following changes are to be performed:\n"
        # view_changes(binary, lines_by_file)
        # choice = raw_input("Apply changes? (y/n) ")
        # if choice == 'y' or choice == 'Y':
        #     format_changes(binary, lines_by_file)
        #     print 'Done.'
        # else:
        #     print 'Done.'
    os.remove(diff_file)

if __name__ == '__main__':
    main()
