#!/usr/bin/python

import sys, csv, os, os.path, re

if len(sys.argv) < 2:
    print "Converts a CSV file to ofono-efl contacts database."
    print
    print "CSV file should have the following columns:"
    print "\t - first name"
    print "\t - last name"
    print "\t - work phone"
    print "\t - home phone"
    print "\t - mobile phone"
    raise SystemExit("missing input CSV file")

reader = csv.reader(open(sys.argv[1], 'rb'))

tmp = os.tmpnam()
tmpfile = open(tmp, 'wb')

tmpfile.write("""\
group "Contacts_List" struct {
    group "list" list {
""")

cleannumber = re.compile("[^0-9]")

for row in reader:
    first, last, work, home, mobile = row

    first = first.strip()
    last = last.strip()

    work = cleannumber.sub('', work)
    home = cleannumber.sub('', home)
    mobile = cleannumber.sub('', mobile)

    print "Add: %s, %s, %s, %s, %s" % (first, last, work, home, mobile)

    tmpfile.write("""
        group "Contact_Info" struct {
            value "picture" string: "";
            value "work" string: "%(work)s";
            value "home" string: "%(home)s";
            value "mobile" string: "%(mobile)s";
            value "first_name" string: "%(first)s";
            value "last_name" string: "%(last)s";
        }
""" % {"first": first, "last": last,
       "work": work, "home": home, "mobile": mobile})

tmpfile.close()

dbfile = os.path.expanduser("~/.config/ofono-efl/contacts.eet")

os.system("eet -e '%s' contacts '%s' 1" % (dbfile, tmp))
os.unlink(tmp)

print "Created %s" % (dbfile,)
print
print "You can decompile this database with the following command:"
print "\teet -d %s contacts /tmp/contacts.txt" % (dbfile,)
print
print "Edit and then recompile it with the following command:"
print "\teet -e %s contacts /tmp/contacts.txt" % (dbfile,)
print
