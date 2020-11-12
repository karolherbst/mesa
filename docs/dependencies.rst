Dependencies
============

As Mesa is used in most distributions by default choosing and adding
dependencies demands for special consideration before accepting patching
adding new ones.

Sometimes we also have in tree experiments where distributions and users are
encouraged to not use them by default. As long there is always a **default**
which is also the generally preferred option, some of the following rules can
be ignored on a case by case basis.

Open Development Model
----------------------

All externel dependencies except OS provided core libraries must follow an Open
Development Model where new patches are reviewed and discussed in public.

Distribution acceptance
-----------------------

All dependencies relevant for Linux builds need to have some sort of acceptence
in the biggest distributions, either by being shipped already or maintainer
having stated in public that they are willing to accept new packaging of the
dependencies in question.

LLVM
----

Dependencies being an LLVM fork or can't keep up with LLVM releases are not
acceptable. We can't require from distribution to package multiple LLVM forks
nor do we want to get held back by dependencies from using the latest LLVM
releases.

Specifically for backend compilers there is a strong preference of writing an
in tree NIR based compiler to keep compilation time minimal and being able to
fix bugs in mesa without having to rely on a new LLVM release.
