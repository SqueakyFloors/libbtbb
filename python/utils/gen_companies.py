#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Regenerate lib/src/companies.c from the Bluetooth SIG's assigned company
# identifiers.
#
#   curl -LO https://bitbucket.org/bluetooth-SIG/public/raw/main/assigned_numbers/company_identifiers/company_identifiers.yaml
#   python3 python/utils/gen_companies.py company_identifiers.yaml lib/src/companies.c
#
# The SIG's list is authoritative for every identifier it carries. It drops an
# identifier when its holder stops being a member, so an identifier the
# committed table names that the list leaves out is carried forward: those
# devices are still on the air, and a capture of one should still decode by
# name. Running this over the committed table therefore keeps the legacy names
# without anyone having to know which they are.
#
# The header comment of the committed file is kept as it stands, apart from the
# date on its "regenerated" line.

import datetime
import re
import sys

import yaml

CASE = re.compile(r'\tcase (\d+):\n\t\treturn "((?:[^"\\]|\\.)*)";')

# 0xFFFF is not an assigned identifier; the table gives it its own case.
SPECIAL = 0xFFFF


def unescape(s):
    return s.replace('\\"', '"').replace("\\\\", "\\")


def escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def read_table(text):
    table = {int(m.group(1)): unescape(m.group(2)) for m in CASE.finditer(text)}
    table.pop(SPECIAL, None)
    return table


def main(argv):
    if len(argv) != 3:
        sys.exit("usage: gen_companies.py <company_identifiers.yaml> <companies.c>")
    yaml_path, c_path = argv[1], argv[2]

    with open(yaml_path, encoding="utf-8") as f:
        entries = yaml.safe_load(f)["company_identifiers"]
    sig = {}
    for e in entries:
        value = int(e["value"])
        if value in sig:
            sys.exit("duplicate identifier 0x%04X in the SIG list" % value)
        sig[value] = e["name"]

    with open(c_path, encoding="utf-8") as f:
        old_text = f.read()
    old = read_table(old_text)
    carried = sorted(set(old) - set(sig))

    table = dict(old)
    table.update(sig)

    header = old_text.split("#include <stdint.h>")[0]
    today = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    header = re.sub(r"regenerated on \d{4}-\d{2}-\d{2}",
                    "regenerated on %s" % today, header)
    header = re.sub(r"The \d+ identifiers the earlier",
                    "The %d identifiers the earlier" % len(carried),
                    header)

    out = [header, "#include <stdint.h>\n", "\n",
           '#include "btbb.h"\n', "\n",
           "const char *btbb_company_name(uint16_t id)\n", "{\n",
           "\tswitch (id) {\n"]
    for value in sorted(table):
        out.append("\tcase %d:\n" % value)
        out.append('\t\treturn "%s";\n' % escape(table[value]))
    out += ["\tcase %d:\n" % SPECIAL,
            '\t\treturn "internal use";\n',
            "\tdefault:\n",
            "\t\treturn NULL;\n",
            "\t}\n", "}\n"]
    with open(c_path, "w", encoding="utf-8") as f:
        f.writelines(out)

    print("%d identifiers, 0x0000 to 0x%04X: %d from the SIG list, %d carried "
          "forward from the committed table"
          % (len(table), max(table), len(sig), len(carried)))


if __name__ == "__main__":
    main(sys.argv)
