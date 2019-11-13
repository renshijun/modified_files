The sqstdrex.cpp is modified to accept lazy match.

To extract the name and hyperlinks of package.html. The following is ok:

sq capture_html_rex.cc packages.html "<dt&gt;.+?&gt;(.+?) - .+?</dt&gt;.+?<dd&gt;.+?Download:.+?&gt;(.+?)<.+?</dd&gt;" 1 2
  
  to obtain only name of packages, issue:
  
  sq capture_html_rex.cc packages.html "<dt>.+?>(.+?) - .+?</dt>.+?<dd>.+?Download:.+?>(.+?)<.+?</dd>" 1
  
  to obtain only hyperlinks in package.html, issue:
  
  sq capture_html_rex.cc packages.html "<dt>.+?>(.+?) - .+?</dt>.+?<dd>.+?Download:.+?>(.+?)<.+?</dd>" 2
