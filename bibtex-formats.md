The 14 BibTeX entry types

Possibly the most difficult aspect of using BibTeX to manage bibliographies is deciding what entry type to use for a reference source. We list all the 14 BibTeX entry types including their description on when to use.
article

An article from a journal, magazine, newspaper, or periodical.

```
@article{CitekeyArticle,
  author   = "P. J. Cohen",
  title    = "The independence of the continuum hypothesis",
  journal  = "Proceedings of the National Academy of Sciences",
  year     = 1963,
  volume   = "50",
  number   = "6",
  pages    = "1143--1148",
}
```

book

A book where the publisher is clearly identifiable.

```
@book{CitekeyBook,
  author    = "Leonard Susskind and George Hrabovsky",
  title     = "Classical mechanics: the theoretical minimum",
  publisher = "Penguin Random House",
  address   = "New York, NY",
  year      = 2014
}
```

booklet

A printed work that is bound, but does not have a clearly identifiable publisher or supporting institution.

```
@booklet{CitekeyBooklet,
  title        = "Canoe tours in {S}weden",
  author       = "Maria Swetla", 
  howpublished = "Distributed at the Stockholm Tourist Office",
  month        = jul,
  year         = 2015
}
```

conference

An article that has been included in conference proceedings. See inproceedings for details.
inbook

A section, such as a chapter, or a page range within a book.

```
@inbook{CitekeyInbook,
  author    = "Lisa A. Urry and Michael L. Cain and Steven A. Wasserman and Peter V. Minorsky and Jane B. Reece",
  title     = "Photosynthesis",
  booktitle = "Campbell Biology",
  year      = "2016",
  publisher = "Pearson",
  address   = "New York, NY",
  pages     = "187--221"
}
```

incollection

A titled section of a book. Such as a short story within the larger collection of short stories that make up the book.

```
@incollection{CitekeyIncollection,
  author    = "Shapiro, Howard M.",
  editor    = "Hawley, Teresa S. and Hawley, Robert G.",
  title     = "Flow Cytometry: The Glass Is Half Full",
  booktitle = "Flow Cytometry Protocols",
  year      = 2018,
  publisher = "Springer",
  address   = "New York, NY",
  pages     = "1--10"
}
```

inproceedings

A paper that has been published in conference proceedings. The usage of conference and inproceedings is the same. The conference entry was included for Scribe compatibility.

```
@inproceedings{CitekeyInproceedings,
  author    = "Holleis, Paul and Wagner, Matthias and Koolwaaij, Johan",
  title     = "Studying mobile context-aware social services in the wild",
  booktitle = "Proc. of the 6th Nordic Conf. on Human-Computer Interaction",
  series    = "NordiCHI",
  year      = 2010,
  pages     = "207--216",
  publisher = "ACM",
  address   = "New York, NY"
}
```

manual

A technical manual for a machine software such as would come with a purchase to explain operation to the new owner.

```
@manual{CitekeyManual,
  title        = "{R}: A Language and Environment for Statistical Computing",
  author       = "{R Core Team}",
  organization = "R Foundation for Statistical Computing",
  address      = "Vienna, Austria",
  year         = 2018
}
```

mastersthesis

A thesis written for the Master’s level degree.

```
@mastersthesis{CitekeyMastersthesis,
  author  = "Jian Tang",
  title   = "Spin structure of the nucleon in the asymptotic limit",
  school  = "Massachusetts Institute of Technology",
  year    = 1996,
  address = "Cambridge, MA",
  month   = sep
}
```

misc

Used if none of the other entry types quite match the source. Frequently used to cite web pages, but can be anything from lecture slides to personal notes.

```
@misc{CitekeyMisc,
  title        = "Pluto: The 'Other' Red Planet",
  author       = "{NASA}",
  howpublished = "\url{https://www.nasa.gov/nh/pluto-the-other-red-planet}",
  year         = 2015,
  note         = "Accessed: 2018-12-06"
}
```

phdthesis

A thesis written for the PhD level degree.

```
@phdthesis{CitekeyPhdthesis,
  author  = "Rempel, Robert Charles",
  title   = "Relaxation Effects for Coupled Nuclear Spins",
  school  = "Stanford University",
  address = "Stanford, CA",
  year    = 1956,
  month   = jun
}
```

proceedings

A conference proceeding.

```
@proceedings{CitekeyProceedings,
  editor    = "Susan Stepney and Sergey Verlan",
  title     = "Proceedings of the 17th International Conference on Computation and Natural Computation, Fontainebleau, France",
  series    = "Lecture Notes in Computer Science",
  volume    = "10867",
  publisher = "Springer",
  address   = "Cham, Switzerland",
  year      = 2018
}
```

techreport

An institutionally published report such as a report from a school, a government organization, an organization, or a company. This entry type is also frequently used for white papers and working papers.

```
@techreport{CitekeyTechreport,
  title       = "{W}asatch {S}olar {P}roject Final Report",
  author      = "Bennett, Vicki and Bowman, Kate and Wright, Sarah",
  institution = "Salt Lake City Corporation",
  address     = "Salt Lake City, UT",
  number      = "DOE-SLC-6903-1",
  year        = 2018,
  month       = sep
}
```

unpublished

A document that has not been officially published such as a paper draft or manuscript in preparation.

```
@unpublished{CitekeyUnpublished,
  author = "Mohinder Suresh",
  title  = "Evolution: a revised theory",
  year   = 2006
}
```