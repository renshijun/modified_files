The sqstdrex.cpp is modified to accept lazy match.

To extract the name and hyperlinks of package.html. The following is ok:

sq capture_html_rex.cc packages.html "<dt&gt;.+?&gt;(.+?) - .+?</dt&gt;.+?<dd&gt;.+?Download:.+?&gt;(.+?)<.+?</dd&gt;" 1 2
  
  to obtain only name of packages, issue:
  
  sq capture_html_rex.cc packages.html "<dt&gt;.+?&gt;(.+?) - .+?</dt&gt;.+?<dd&gt;.+?Download:.+?&gt;(.+?)<.+?</dd&gt;" 1
  
  to obtain only hyperlinks in package.html, issue:
  
  sq capture_html_rex.cc packages.html "<dt&gt;.+?&gt;(.+?) - .+?</dt&gt;.+?<dd&gt;.+?Download:.+?&gt;(.+?)<.+?</dd&gt;" 2





file sss.cc:

function formatCapture(cap) {
if (cap == null) return "no match";
// If there is a match, show the start and end index, and the length
else return format("[%d,%d] (%d)", cap[0].begin, cap[0].end, cap[0].end - cap[0].begin);
}
local re = regexp("a.*bc");

local strs = [
"a bcd",
"a bc bcd",
"a b bcd",
];

foreach (fff in strs)
{
re = regexp("a.*bc");
print(formatCapture(re.capture(fff)) + "\n");
}

returns:

[root@etchosts test_scripts]# ../bin/sq sss.cc
[0,4] (4)
[0,7] (7)
[0,6] (6)

======================================================
file sss1.cc:

function formatCapture(cap) {
if (cap == null) return "no match";
// If there is a match, show the start and end index, and the length
else return format("[%d,%d] (%d)", cap[0].begin, cap[0].end, cap[0].end - cap[0].begin);
}
local re = regexp("a.*bc");

local strs = [
"a bcd",
"a bc bcd",
"a b bcd",
];

foreach (fff in strs)
{
print(formatCapture(re.capture(fff)) + "\n");
}

returns:
[root@etchosts test_scripts]# ../bin/sq sss1.cc
[0,4] (4)
[0,4] (4)
[0,4] (4)

=============================================================
file sss2.cc:

function formatCapture(cap) {
if (cap == null) return "no match";
// If there is a match, show the start and end index, and the length
else return format("[%d,%d] (%d)", cap[0].begin, cap[0].end, cap[0].end - cap[0].begin);
}
local re = regexp("a.*?bc");

local strs = [
"a bcd",
"a bc bcd",
"a b bcd",
];

foreach (fff in strs)
{
re = regexp("a.*?bc");

print(formatCapture(re.capture(fff)) + "\n");
}

returns:

[root@etchosts test_scripts]# ../bin/sq sss2.cc
[0,4] (4)
[0,4] (4)
[0,6] (6)
