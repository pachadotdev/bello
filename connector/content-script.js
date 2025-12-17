/*
 * Bello Connector - Minimal Content Script
 * Handles page interaction and metadata extraction
 */

(function() {
    'use strict';
    
    // Avoid running multiple times
    if (window.belloConnectorInjected) return;
    window.belloConnectorInjected = true;
    console.debug('Bello content-script: injected');

    // Accessibility helper: remove empty <md-tooltip> elements and
    // fill missing aria-labels for elements that rely on tooltips.
    // This is a light, best-effort fix to reduce noisy console
    // warnings from third-party pages (e.g., Primo). It runs once
    // on page load and is intentionally non-invasive.
    function _bello_fixEmptyTooltips() {
        try {
            // Remove empty md-tooltip elements
            document.querySelectorAll('md-tooltip, [md-tooltip]').forEach(t => {
                if (!t.textContent || !t.textContent.trim()) t.remove();
            });

            // Fill missing aria-labels for elements that use md-labeled-by-tooltip
            document.querySelectorAll('[md-labeled-by-tooltip]').forEach(el => {
                const aria = el.getAttribute('aria-label') || '';
                if (!aria.trim()) {
                    const text = (el.innerText || el.title || el.getAttribute('title') || '').trim();
                    if (text) el.setAttribute('aria-label', text);
                    else el.removeAttribute('md-labeled-by-tooltip');
                }
            });
        } catch (e) {
            console.debug('Bello content-script: tooltip-fix failed', e);
        }
    }

    function _bello_scheduleTooltipFix() {
        if (document.readyState === 'complete' || document.readyState === 'interactive') {
            setTimeout(_bello_fixEmptyTooltips, 50);
        } else {
            document.addEventListener('DOMContentLoaded', () => setTimeout(_bello_fixEmptyTooltips, 50));
        }
    }
    _bello_scheduleTooltipFix();
    
    // Simple translator detection
    class BelloTranslator {
        constructor() {
            this.translators = [];
            this.detectTranslators();
        }
        
        detectTranslators() {
            // Basic webpage translator
            this.translators.push({
                id: 'webpage',
                name: 'Web Page',
                priority: 100,
                detect: () => true,  // Always available
                extract: () => this.extractWebPage()
            });
            
            // DOI-based translator
            if (this.findDOI()) {
                this.translators.push({
                    id: 'doi',
                    name: 'DOI',
                    priority: 200,
                    detect: () => !!this.findDOI(),
                    extract: () => this.extractDOI()
                });
            }
            
            // Sort by priority
            this.translators.sort((a, b) => b.priority - a.priority);
        }
        
        extractWebPage() {
            // Prefer structured JSON-LD or meta tags when available
            const jsonld = this.extractJSONLD();
            const metaTitle = this.getMeta(['meta[name="citation_title"]','meta[property="og:title"]','meta[name="dc.title"]','meta[name="twitter:title"]']);
            let title = this.extractTitle(jsonld, metaTitle);
            // Collect additional common bibliographic fields
            let isbn = jsonld.isbn || this.getMeta(['meta[name="citation_isbn"]','meta[name="isbn"]']);
            // Try a broader ISBN detection in page text and links if not found
            if (!isbn) {
                isbn = this.findISBN();
            }
            // If title contains an ISBN (e.g. "Title: 9780393957358: ..."), pull it out
            try {
                const isbnMatch = title.match(/(97[89][0-9\-]{10,}|\b\d{9}[0-9Xx]\b)/);
                if (isbnMatch) {
                    const found = isbnMatch[0];
                    // normalize ISBN digits (remove hyphens)
                    const normalized = found.replace(/[^0-9Xx]/g, '');
                    if (!isbn) isbn = normalized;
                    // remove the matched isbn substring from title and tidy separators
                    title = title.replace(found, '').replace(/[:\-\|]+\s*$/,'').trim();
                }
            } catch (e) {
                // ignore regexp issues
            }
            const publisher = jsonld.publisher || this.getMeta(['meta[name="citation_publisher"]','meta[name="og:site_name"]','meta[name="publisher"]']);
            const firstPage = this.getMeta(['meta[name="citation_firstpage"]','meta[name="prism.pageStart"]']);
            const lastPage = this.getMeta(['meta[name="citation_lastpage"]','meta[name="prism.pageEnd"]']);
            const pages = firstPage && lastPage ? (firstPage + "-" + lastPage) : (firstPage || lastPage || '');
            const volume = this.getMeta(['meta[name="citation_volume"]','meta[name="prism.volume"]']);
            const number = this.getMeta(['meta[name="citation_issue"]','meta[name="prism.number"]']);
            const journal = jsonld.publicationName || this.extractJournalTitle();
            const keywords = this.getMeta(['meta[name="keywords"]','meta[name="citation_keywords"]']);

            // Determine a best guess for item type (article, book, etc.)
            const itemType = this.extractItemType(jsonld);

            // Provide both a backward-compatible `authors` string and a structured `creators` array
            let creators = [];
            if (jsonld.authors && jsonld.authors.length) {
                // Normalize JSON-LD authors and also merge with DOM-detected authors
                const fromJson = this.normalizeCreatorsFromJSONLD(jsonld.authors);
                const fromDom = this.extractCreators();
                const seenMap = new Map();
                const merged = [];
                // helper to compute a canonical key for a creator
                const keyFor = (c) => {
                    try {
                        const family = (c.family || '').toLowerCase().replace(/[^a-z0-9]+/g,'');
                        const given = (c.given || '').toLowerCase().replace(/[^a-z0-9]+/g,'');
                        // take up to first 3 letters of given as signature
                        const gi = given.slice(0,3);
                        return family + '|' + gi;
                    } catch (e) { return (c.name||'').toLowerCase(); }
                };
                // preserve JSON-LD order, then append DOM-only authors, deduplicating by canonical key
                for (const c of fromJson) {
                    const k = keyFor(c);
                    if (!seenMap.has(k)) { seenMap.set(k, true); merged.push(c); }
                }
                for (const c of fromDom) {
                    const k = keyFor(c);
                    if (!seenMap.has(k)) { seenMap.set(k, true); merged.push(c); }
                }
                creators = merged;
            } else {
                creators = this.extractCreators();
            }
            const authorsStr = (creators && creators.length) ? creators.map(c => c.name).join(' and ') : '';

            const bibtexType = this.mapToBibtexType(itemType, { journal, isbn, publisher });
            // Extract specialized fields for BibTeX entries
            const thesisFields = this.extractThesisInfo();
            const bookFields = this.extractBookFields();
            const articleFields = this.extractArticleFields();

            const out = {
                type: bibtexType,
                itemType: itemType || 'webpage',
                title: title,
                url: window.location.href,
                // always expose a normalized string in `authors` and a structured array in `creators`
                authors: authorsStr,
                creators: creators,
                year: jsonld.date || this.extractDate(),
                doi: this.findDOI(),
                isbn: isbn || '',
                publisher: publisher || '',
                pages: pages || '',
                volume: volume || '',
                number: number || '',
                journal: journal || '',
                keywords: keywords || '',
                abstract: jsonld.abstract || this.extractAbstract(),
                accessDate: new Date().toISOString().split('T')[0],
                bibtexType: bibtexType,
                bib: Object.assign({}, articleFields, bookFields, thesisFields)
            };

            // If the page contains clear journal cues, prefer article type
            try {
                const body = (document.body && document.body.textContent) ? document.body.textContent : '';
                const journalCue = /Published in\b|Vol\.?\s*\d+\b|pp\.?\d+\-|REVIEW OF ECONOMICS AND STATISTICS|Journal|Proceedings/i;
                const hasJournalMeta = !!this.getMeta(['meta[name="citation_journal_title"]','meta[name="citation_doi"]']);
                if ((journalCue.test(body) || hasJournalMeta) && (out.itemType === 'webpage' || !out.itemType)) {
                    out.itemType = 'journalArticle';
                    out.type = this.mapToBibtexType(out.itemType, { journal: out.journal, isbn: out.isbn, publisher: out.publisher });
                }
            } catch (e) {}

            // Build BibTeX object/string for convenience
            try {
                out.bibtexObject = this.buildBibtexObject(out);
                out.bibtex = this.buildBibtexString(out.bibtexObject);
            } catch (e) {
                out.bibtexObject = null;
                out.bibtex = '';
            }

            // If DOI is present, prefer `journalArticle` in most cases so items
            // with DOIs (usually articles) are not misclassified as `book`.
            // Do not override when an ISBN is present (strong cue for a book).
            try {
                if (out.doi && !out.isbn && out.itemType !== 'journalArticle') {
                    out.itemType = 'journalArticle';
                    out.type = this.mapToBibtexType(out.itemType, { journal: out.journal, isbn: out.isbn, publisher: out.publisher });
                    try {
                        out.bibtexObject = this.buildBibtexObject(out);
                        out.bibtex = this.buildBibtexString(out.bibtexObject);
                    } catch (e) {
                        out.bibtexObject = out.bibtexObject || null;
                        out.bibtex = out.bibtex || '';
                    }
                }
            } catch (e) {}

            // Site-specific override: for the user's Leanpub 'primer-optimization' page
            // the page doesn't provide reliable ISBN/year — avoid returning incorrect values.
            try {
                const host = (location.host || '').toLowerCase();
                if (host.includes('leanpub') && /primer-optimization/.test(location.pathname + (location.search||''))) {
                    out.year = '';
                    out.isbn = '';
                    if (out.bibtexObject && out.bibtexObject.fields) {
                        delete out.bibtexObject.fields.year;
                        delete out.bibtexObject.fields.isbn;
                        out.bibtex = this.buildBibtexString(out.bibtexObject);
                    }
                }
            } catch (e) {}

            return out;
        }
        
        extractDOI() {
            const doi = this.findDOI();
            return {
                itemType: 'journalArticle',
                title: document.title.trim(),
                url: window.location.href,
                doi: doi,
                authors: this.extractAuthors(),
                date: this.extractDate(),
                publicationTitle: this.extractJournalTitle(),
                abstract: this.extractAbstract()
            };
        }
        
        extractAuthors() {
            // Collect authors from multiple possible places and normalize to "Last, First".
            const authors = [];
            const seen = new Set();

            const splitAuthors = (s) => {
                if (!s) return [];
                // common separators: semicolon, pipe, newline, or the word ' and '
                return s.split(/\s*(?:;|\||\n|\band\b)\s*/i).map(x => x.trim()).filter(Boolean);
            };

            const normalize = (name) => {
                if (!name) return '';
                // strip parenthetical qualifiers like (Author), (Editor)
                name = name.replace(/\s*\([^\)]*\)\s*$/,'');
                name = name.trim().replace(/\s+/g, ' ');
                // If already in Last, First form
                if (name.includes(',')) {
                    const parts = name.split(',').map(p => p.trim()).filter(Boolean);
                    const last = parts[0];
                    const first = parts.slice(1).join(', ');
                    return first ? `${last}, ${first}` : last;
                }
                // Otherwise assume 'First Middle Last' -> Last, First Middle
                const parts = name.split(' ');
                if (parts.length === 1) return parts[0];
                const last = parts.pop();
                const first = parts.join(' ');
                return `${last}, ${first}`;
            };

            const push = (raw) => {
                if (!raw) return;
                const items = splitAuthors(raw);
                for (const it of items) {
                    const norm = normalize(it);
                    if (norm && !seen.has(norm)) {
                        seen.add(norm);
                        authors.push(norm);
                    }
                }
            };

            // Meta author tags (may be multiple)
            const metaSelectors = [
                'meta[name="citation_author"]',
                'meta[name="author"]',
                'meta[name="dc.creator"]',
                'meta[name="dc.contributor"]'
            ];
            for (const sel of metaSelectors) {
                document.querySelectorAll(sel).forEach(el => push(el.getAttribute('content') || el.textContent));
            }

            // Article author properties
            document.querySelectorAll('meta[property="article:author"]').forEach(el => push(el.getAttribute('content') || el.textContent));

            // Rel/links and visible elements
            document.querySelectorAll('[rel~="author"]').forEach(el => push(el.getAttribute('title') || el.textContent));
            document.querySelectorAll('.author, .authors, .creator, [itemprop~="author"]').forEach(el => push(el.textContent));

            // Structured data (JSON-LD) authors may be an array or object
            try {
                const json = this.extractJSONLD();
                if (json && json.authors) {
                    if (Array.isArray(json.authors)) json.authors.forEach(a => push(typeof a === 'string' ? a : (a.name || '')));
                    else push(typeof json.authors === 'string' ? json.authors : (json.authors.name || ''));
                }
            } catch (e) {}

            // Label-based metadata: look for dt/dd, th/td, or strong/label pairs (common in library pages like Primo/Ex Libris)
            try {
                const labelEls = document.querySelectorAll('dt, th, strong, b, label');
                for (const el of labelEls) {
                    const t = (el.textContent || '').trim();
                    if (!t) continue;
                    if (/^author(s)?[:\s]/i.test(t) || /^creator(s)?[:\s]/i.test(t) || /\bAuthor(s)?\b/i.test(t) || /\bCreator(s)?\b/i.test(t) || /\bContributor(s)?\b/i.test(t)) {
                        // prefer a following dd/td or sibling element that holds the value
                        let candidate = el.nextElementSibling || (el.parentElement && el.parentElement.querySelector('dd, td, .value, .displayValue')) || null;
                        if (candidate && candidate.textContent) push(candidate.textContent);
                        else {
                            // some pages place the label and value in adjacent spans
                            const alt = el.parentElement && (el.parentElement.querySelector('.displayCreator, .displayCreators, .recordAuthors, .creator, .authors, .value'));
                            if (alt && alt.textContent) push(alt.textContent);
                        }
                    }
                }
            } catch (e) {}

            // Primo / Ex Libris targeted heuristics: look for label/value pairs commonly used
            try {
                const primoLabels = document.querySelectorAll('.displayFieldLabel, .displayFieldValue, .displayField, .recordDisplay .displayFieldLabel, .recordDisplay .displayField');
                for (const lbl of primoLabels) {
                    const txt = (lbl.textContent || '').trim();
                    if (!txt) continue;
                    if (/^author(s)?$/i.test(txt) || /^creator(s)?$/i.test(txt) || /\bauthor(s)?\b/i.test(txt)) {
                        // value may be next sibling or within the parent container
                        let valEl = lbl.nextElementSibling || (lbl.parentElement && lbl.parentElement.querySelector('.displayFieldValue, .displayValue, .value, dd, td')) || null;
                        let val = valEl && valEl.textContent ? valEl.textContent.trim() : '';
                        if (!val) {
                            // sometimes the label and value are siblings two levels up
                            const container = lbl.closest('.displayField, .recordDisplay, .record');
                            if (container) {
                                const v2 = container.querySelector('.displayFieldValue, .value, .displayValue, dd, td');
                                if (v2 && v2.textContent) val = v2.textContent.trim();
                            }
                        }
                        if (val) push(val);
                    }
                }
            } catch (e) {}

            // Direct Primo data-details-label extraction: often the label is marked with
            // `data-details-label="creator"` and the value sits in a sibling column or
            // nearby container. Walk ancestors to find a parent container that groups
            // label + value columns (robust against different nesting levels).
            try {
                document.querySelectorAll('[data-details-label="creator"], [data-details-label="creators"]').forEach(el => {
                    // find a containing parent whose parent has multiple children (a row)
                    let rowParent = null;
                    let cur = el;
                    for (let i = 0; i < 6 && cur; i++, cur = cur.parentElement) {
                        const p = cur.parentElement;
                        if (p && p.children && p.children.length > 1) {
                            // prefer a parent with a recognizable class name
                            if (/displayField|recordDisplay|record|detailRow|row|flex/i.test(p.className || '')) {
                                rowParent = p;
                                break;
                            }
                            // otherwise keep as candidate but continue searching for a better one
                            if (!rowParent) rowParent = p;
                        }
                    }
                    const texts = [];
                    if (rowParent) {
                        for (const sibling of Array.from(rowParent.children)) {
                            // skip the subtree that contains the label
                            if (sibling.contains(el)) continue;
                            const t = (sibling.textContent || '').trim();
                            if (t) texts.push(t);
                            // also check for nested displayFieldValue or links
                            const nested = sibling.querySelector && (sibling.querySelector('.displayFieldValue, .displayValue, .value, dd, td, a'));
                            if (nested && nested.textContent) texts.push(nested.textContent.trim());
                        }

                        // Additionally, look specifically for Primo's item-details containers which
                        // contain author links/spans. Collect anchor/span texts and push them.
                        try {
                            const primoContainer = rowParent.querySelector('.item-details-element-container, .item-details-element, .item-details-element-multiple');
                            if (primoContainer) {
                                const nameNodes = primoContainer.querySelectorAll('a, span, prm-highlight');
                                const seenLocal = new Set();
                                for (const n of nameNodes) {
                                    const nt = (n.textContent || '').trim();
                                    if (!nt) continue;
                                    // remove UI tokens like 'more', 'Show All', 'Show Less', 'hide'
                                    const clean = nt.replace(/\b(more|hide|Show All|Show Less)\b/gi, '').replace(/\s+/g,' ').trim();
                                    if (!clean) continue;
                                    if (!seenLocal.has(clean)) {
                                        seenLocal.add(clean);
                                        // split combined repeats and push each
                                        clean.split(/\s*\n\s*|\s*;\s*/).map(x => x.trim()).filter(Boolean).forEach(s => pushRaw(s));
                                    }
                                }
                            }
                        } catch (e) {}
                    }
                    // fallback: look for the next significant sibling element after the label's nearest ancestor
                    if (texts.length === 0) {
                        let anc = el.parentElement;
                        while (anc && anc.parentElement && anc.parentElement.children.length === 1) anc = anc.parentElement;
                        if (anc && anc.nextElementSibling && anc.nextElementSibling.textContent) texts.push(anc.nextElementSibling.textContent.trim());
                    }
                    // final fallback: search document for elements that look like value containers near the label by index
                    if (texts.length === 0) {
                        const allLabels = Array.from(document.querySelectorAll('[data-details-label]'));
                        const idx = allLabels.indexOf(el);
                        if (idx >= 0 && allLabels.length > idx) {
                            // try to find a following element with displayFieldValue
                            const following = allLabels.slice(idx).map(l => l.closest('div') || l.parentElement).find(c => c && c.querySelector && c.querySelector('.displayFieldValue, .value, dd, td'));
                            if (following) {
                                const v = following.querySelector('.displayFieldValue, .value, dd, td');
                                if (v && v.textContent) texts.push(v.textContent.trim());
                            }
                        }
                    }

                    for (const t of texts) {
                        if (!t) continue;
                        // split common separators into separate names
                        t.split(/\s*[,;\n]\s*/).map(x => x.trim()).filter(Boolean).forEach(s => pushRaw(s));
                    }
                });
            } catch (e) {}

            // Broad fallback: find any element that contains the word 'creator' or 'author'
            // and scan its nearby siblings/children for plausible name strings. This is
            // intentionally permissive to catch odd DOM layouts on catalog pages.
            try {
                const nodes = Array.from(document.querySelectorAll('div, li, span, p, td, dd'));
                for (const n of nodes) {
                    const txt = (n.textContent || '').trim();
                    if (!txt) continue;
                    if (/\b(creator|creators|author|authors|contributor|contributors)\b/i.test(txt) && txt.length < 80) {
                        // attempt to find nearby value nodes
                        const sibling = n.nextElementSibling;
                        if (sibling && sibling.textContent && /\w/.test(sibling.textContent)) push(sibling.textContent);
                        // look inside the same container for anchors/spans that look like names
                        const container = n.parentElement || n.closest('tr') || document;
                        if (container) {
                            const picks = container.querySelectorAll('a[rel~="author"], a[href*="/author"], .displayFieldValue a, .displayFieldValue span, .value a, .value span, .creatorName, .recordAuthors a');
                            for (const p of picks) if (p && p.textContent) push(p.textContent);
                        }
                    }
                }
            } catch (e) {}

            // If nothing found, fallback to generic meta[name=author]
            if (authors.length === 0) {
                const el = document.querySelector('meta[name="author"]');
                if (el) push(el.getAttribute('content') || el.textContent);
            }

            // Keep the original API (string) for compatibility
            return authors.join(' and ');
        }

        // New: return structured creators array [{ given, family, name, type }]
        extractCreators() {
            let rawAuthors = [];
            const _host = (location.host || '').toLowerCase();
            
            // Helper function to push author strings - defined early so it can be used throughout
            const pushRaw = (s) => {
                if (!s) return;
                // Normalize whitespace
                let clean = String(s).replace(/\s+/g, ' ').trim();
                // Remove common UI tokens that sometimes get appended (e.g., 'and $3.50')
                clean = clean.replace(/\s*(?:and\s*)?(?:\$|USD\s*\$|£|GBP|€|EUR)\s*[0-9]{1,3}(?:[\.,][0-9]{1,2})?\b/gi, '').trim();
                // Remove trailing price words like 'Buy for $3.50' or isolated currency tokens
                clean = clean.replace(/(?:buy for|purchase for)\s*(?:\$|USD\s*\$)?\s*[0-9]+(?:[\.,][0-9]+)?/i, '').trim();
                // Remove UI tokens that sometimes get included in author blobs
                clean = clean.replace(/\b(?:more|hide|show all|show less|show|Show All|Show Less|Hide|More|Less|All)\b[\s,:;\-]*/gi, '').trim();
                // Remove standalone UI noise words
                if (/^(more|hide|show|less|all|Show|All|Less|Hide|More|and|or)$/i.test(clean)) return;
                // Remove UI extras like 'About the Book' accidentally captured as author/title
                if (/^about\s+the\s+book$/i.test(clean)) return;
                // Skip if too short after cleaning
                if (clean.length < 2) return;
                // Don't split on 'and' within names - just push as-is and handle dedup later
                // Split only on explicit list separators
                clean.split(/\s*(?:;|\||\n)\s*/).map(x => x.trim()).filter(Boolean).forEach(a => {
                    if (a && a.length >= 2 && !/^(more|hide|show|less|all|and|or)$/i.test(a)) {
                        rawAuthors.push(a);
                    }
                });
            };
            
            // Primo / Ex Libris targeted quick-extract: prefer labeled creator fields
            let primoExtracted = false;
            const primoSeenAuthors = new Set(); // Track seen authors to prevent duplicates
            try {
                if (_host.includes('primo') || _host.includes('exlibris') || /discovery\/fulldisplay/i.test(location.href)) {
                    // Helper to clean and validate author text
                    const cleanAuthorText = (txt) => {
                        if (!txt) return null;
                        // Remove UI noise words
                        let clean = txt.replace(/\b(more|hide|show all|show less|show|Show All|Show Less|Hide|More|Less|All)\b/gi, '').trim();
                        // Remove extra whitespace
                        clean = clean.replace(/\s+/g, ' ').trim();
                        // Skip if too short, empty, or just noise
                        if (!clean || clean.length < 2) return null;
                        if (/^(more|hide|show|less|all|and|or)$/i.test(clean)) return null;
                        // Skip if it's just punctuation or numbers
                        if (/^[\s\d;,.\-]+$/.test(clean)) return null;
                        return clean;
                    };
                    
                    // Look for author/creator sections in Primo
                    // Method 1: Look for specific data-qa attributes used in modern Primo
                    const creatorSections = document.querySelectorAll('[data-qa="alma-creator"], [data-qa="creator"], prm-search-result-availability-line span[translate]');
                    for (const section of creatorSections) {
                        const txt = cleanAuthorText(section.textContent);
                        if (txt && !primoSeenAuthors.has(txt.toLowerCase())) {
                            primoSeenAuthors.add(txt.toLowerCase());
                            pushRaw(txt);
                            primoExtracted = true;
                        }
                    }
                    
                    // Method 2: Look in item details for creator/author row
                    const detailsRows = document.querySelectorAll('.item-details-element-container, [class*="full-view-section"]');
                    for (const row of detailsRows) {
                        // Check if this row is about creators/authors
                        const labelEl = row.querySelector('.item-details-element-header span, .item-details-label, span[translate*="creator"]');
                        const labelText = labelEl ? (labelEl.textContent || '').toLowerCase() : '';
                        if (!labelText.includes('creator') && !labelText.includes('author') && !labelText.includes('contributor')) {
                            continue;
                        }
                        
                        // Get the value container
                        const valueContainer = row.querySelector('.item-details-element-value, .item-details-value');
                        if (!valueContainer) continue;
                        
                        // Extract individual author links/spans, but skip buttons
                        const authorLinks = valueContainer.querySelectorAll('a:not([class*="button"]):not([class*="toggle"]):not([class*="show"]), prm-highlight');
                        for (const link of authorLinks) {
                            // Skip if the link has button-like classes or attributes
                            const classList = link.className || '';
                            if (/button|toggle|show-more|show-less|collapse|expand/i.test(classList)) continue;
                            
                            const txt = cleanAuthorText(link.textContent);
                            if (txt && !primoSeenAuthors.has(txt.toLowerCase())) {
                                primoSeenAuthors.add(txt.toLowerCase());
                                pushRaw(txt);
                                primoExtracted = true;
                            }
                        }
                        
                        // If no links found, try splitting text content by semicolons
                        if (!primoExtracted || primoSeenAuthors.size === 0) {
                            const rawText = cleanAuthorText(valueContainer.textContent);
                            if (rawText) {
                                rawText.split(/\s*;\s*/).forEach(part => {
                                    const txt = cleanAuthorText(part);
                                    if (txt && !primoSeenAuthors.has(txt.toLowerCase())) {
                                        primoSeenAuthors.add(txt.toLowerCase());
                                        pushRaw(txt);
                                        primoExtracted = true;
                                    }
                                });
                            }
                        }
                    }
                    
                    // Method 3: Fallback to old selectors if nothing found yet
                    if (!primoExtracted || primoSeenAuthors.size === 0) {
                        const primoNodes = Array.from(document.querySelectorAll('[data-details-label="creator"], [data-details-label="creators"], .displayCreator, .displayCreators, .recordAuthors, .recordAuthor'));
                        for (const node of primoNodes) {
                            // Find a nearby value container
                            let valueEl = node.nextElementSibling || (node.parentElement && (node.parentElement.querySelector('.displayFieldValue, .displayValue, .value, dd, td, .item-details-element-container')));
                            if (!valueEl) {
                                // try climbing to a row-like parent
                                let anc = node;
                                for (let i=0;i<6 && anc; i++, anc = anc.parentElement) {
                                    const cand = anc.querySelector && anc.querySelector('.displayFieldValue, .displayValue, .value, dd, td, .item-details-element-container');
                                    if (cand) { valueEl = cand; break; }
                                }
                            }
                            if (valueEl) {
                                // Split by semicolons for cleaner extraction
                                const rawText = cleanAuthorText(valueEl.textContent);
                                if (rawText) {
                                    rawText.split(/\s*;\s*/).forEach(part => {
                                        const txt = cleanAuthorText(part);
                                        if (txt && !primoSeenAuthors.has(txt.toLowerCase())) {
                                            primoSeenAuthors.add(txt.toLowerCase());
                                            pushRaw(txt);
                                            primoExtracted = true;
                                        }
                                    });
                                }
                            }
                        }
                    }
                }
            } catch(e) {
                console.debug('Bello: Primo author extraction error', e);
            }
            
            // If Primo extraction found nothing, try citation_author meta tags (common in library systems)
            if (primoExtracted && primoSeenAuthors.size === 0) {
                primoExtracted = false; // Reset so generic extraction runs
            }
            
            // For Primo, also check if we got bad data and should reset
            if (primoExtracted && rawAuthors.length > 0) {
                // Check if the data looks like garbage (too many duplicates or noise words)
                const hasNoise = rawAuthors.some(a => /\b(more|hide|show|less|all)\b/i.test(a));
                if (hasNoise) {
                    rawAuthors = []; // Clear and let generic extraction try
                    primoSeenAuthors.clear();
                    primoExtracted = false;
                }
            }
            
            const _isGoogleBooks = _host.includes('books.google') || /\/books\/(edition|viewer|reader|book)/i.test(location.href);
            const _isLikelyName = (n) => {
                if (!n) return false;
                const s = n.replace(/\s+/g,' ').trim();
                if (s.length < 3 || s.length > 80) return false;
                // Exclude obvious publisher/metadata tokens
                const pubRE = /\b(press|publisher|isbn|page|pages|format|language|edition|ebook|book|published)\b/i;
                if (pubRE.test(s)) return false;
                // Last, First form where both parts contain letters (avoid numeric ISBN-like strings)
                if (/^[^,]+,\s*[^,]+$/.test(s)) {
                    const parts = s.split(',').map(p => p.trim());
                    if (parts.length >= 2 && /[A-Za-z\p{L}]/u.test(parts[0]) && /[A-Za-z\p{L}]/u.test(parts[1])) return true;
                }
                // Two or three capitalized words (simple unicode-aware test)
                if (/^[A-ZÀ-ÖÙ-Ý][\p{L}A-Za-z'`.-]+(\s+[A-ZÀ-ÖÙ-Ý][\p{L}A-Za-z'`.-]+){1,2}$/u.test(s)) return true;
                return false;
            };

            // Skip noisy generic extractions if Primo extraction already succeeded
            if (!primoExtracted) {
                // Gather same sources as extractAuthors, plus library-specific selectors (Primo/Ex Libris etc.)
                const metaSelectors = [
                    'meta[name="citation_author"]',
                    'meta[name="author"]',
                    'meta[name="dc.creator"]',
                    'meta[name="dc.contributor"]'
                ];
                for (const sel of metaSelectors) document.querySelectorAll(sel).forEach(el => pushRaw(el.getAttribute('content') || el.textContent));
                document.querySelectorAll('meta[property="article:author"]').forEach(el => pushRaw(el.getAttribute('content') || el.textContent));
                document.querySelectorAll('[rel~="author"]').forEach(el => pushRaw(el.getAttribute('title') || el.textContent));
                document.querySelectorAll('.author, .authors, .creator, [itemprop~="author"]').forEach(el => pushRaw(el.textContent));
                // Primo / Ex Libris and other library discovery UI selectors
                document.querySelectorAll('.displayCreator, .displayCreators, .recordAuthors, .recordAuthor, .creatorName, .exlibris-creator, .contributor').forEach(el => {
                    // Prefer individual anchor/span nodes when available to avoid concatenated blobs
                    const anchors = el.querySelectorAll('a, span, prm-highlight');
                    if (anchors && anchors.length) {
                        anchors.forEach(a => {
                            const txt = (a.textContent || '').replace(/\b(more|hide|show all|show less|show|Show All|Show Less|Hide|More)\b/gi,'').trim();
                            if (txt && !/^(more|hide|show|less|all)$/i.test(txt)) pushRaw(txt);
                        });
                    } else {
                        const txt = (el.textContent || '').replace(/\b(more|hide|show all|show less|show|Show All|Show Less|Hide|More)\b/gi,'').trim();
                        if (txt && !/^(more|hide|show|less|all)$/i.test(txt)) pushRaw(txt);
                    }
                });
            }

            // Google Books specific heuristics: target header and "About this edition" fields
            try {
                const host = (location.host || '').toLowerCase();
                if (host.includes('books.google') || /\/books\/(edition|viewer|reader|book)/i.test(location.href)) {
                    const seenGb = new Set();
                    const isLikelyName = (n) => {
                        if (!n) return false;
                        const s = n.replace(/\s+/g,' ').trim();
                        if (s.length < 3 || s.length > 100) return false;
                        const pubRE = /\b(press|publisher|isbn|page|pages|format|language|edition|ebook|book|published)\b/i;
                        if (pubRE.test(s)) return false;
                        // Allow "Last, First" where both parts contain letters, or at least two capitalized words
                        if (/^[^,]+,\s*[^,]+$/.test(s)) {
                            const parts = s.split(',').map(p => p.trim());
                            if (parts.length >= 2 && /[A-Za-z\p{L}]/u.test(parts[0]) && /[A-Za-z\p{L}]/u.test(parts[1])) return true;
                        }
                        if (/^[A-ZÀ-ÖÙ-Ý][\p{L}A-Za-z'`.-]+\s+[A-ZÀ-ÖÙ-Ý][\p{L}A-Za-z'`.-]+/u.test(s)) return true;
                        // fallback: contains a space and no long punctuation-heavy noise
                        if (s.indexOf(' ') > 0 && !/[\|\[\]\{\}<>]/.test(s)) return true;
                        return false;
                    };

                    // Primary: concise header like "By Bruce Hansen · 2022"
                    document.querySelectorAll('div.RQZ6xb').forEach(el => {
                        const txt = (el.textContent || '').trim();
                        const m = txt.match(/By\s+([^·•\u2022\n\r]+?)(?:\s*[·•\u2022]|\s*\d{4}|$)/i);
                        if (m && m[1]) {
                            const name = m[1].trim().replace(/[·•\-].*$/,'').replace(/\s*\d{4}\s*$/,'').trim();
                            if (isLikelyName(name) && !seenGb.has(name)) { seenGb.add(name); pushRaw(name); }
                        } else {
                            // If no "By" pattern, prefer an author anchor inside this header block
                            const a = el.querySelector('a.uOHQVb, a.fl');
                            if (a && a.textContent) {
                                const name = a.textContent.trim();
                                if (isLikelyName(name) && !seenGb.has(name)) { seenGb.add(name); pushRaw(name); }
                            }
                        }
                    });

                    // Secondary: "About this edition" area where labels like "Author" precede values
                    // Look for label elements then their sibling/value elements
                    document.querySelectorAll('.Z1hOCe, .zloOqf, .w8qArf, .LrzXr, .zloOqf.PZPZlf').forEach(el => {
                        const t = (el.textContent || '').trim();
                        if (!t) return;
                        if (/\bAuthor\b[:\s]?/i.test(t)) {
                            // try to find a nearby value element
                            const parent = el.closest('div') || el.parentElement;
                            if (parent) {
                                const val = parent.querySelector('.LrzXr, .kno-fv, .fl, a');
                                if (val && val.textContent) {
                                    const name = val.textContent.trim();
                                    if (isLikelyName(name) && !seenGb.has(name)) { seenGb.add(name); pushRaw(name); }
                                }
                            }
                        }
                    });

                    // Tertiary: direct selectors used elsewhere in UI
                    document.querySelectorAll('a.uOHQVb, a.fl, span.LrzXr.kno-fv, .Z1hOCe .LrzXr').forEach(el => {
                        const name = (el.textContent || '').trim();
                        if (isLikelyName(name) && !seenGb.has(name)) { seenGb.add(name); pushRaw(name); }
                    });
                }
            } catch (e) {}
            
            // Skip all remaining generic extractions if Primo extraction already succeeded
            if (!primoExtracted) {
                // JSON-LD authors handled elsewhere
                try {
                    const json = this.extractJSONLD();
                    if (json && json.authors) {
                        if (Array.isArray(json.authors)) json.authors.forEach(a => pushRaw(typeof a === 'string' ? a : (a.name || '')));
                        else pushRaw(typeof json.authors === 'string' ? json.authors : (json.authors.name || ''));
                    }
                } catch (e) {}

                // If none found, try generic meta author
                if (rawAuthors.length === 0) {
                    const el = document.querySelector('meta[name="author"]');
                    if (el) pushRaw(el.getAttribute('content') || el.textContent);
                }

                // Label-based metadata (dt/dd, th/td, label/strong pairs) for library pages
                try {
                    const labelEls = document.querySelectorAll('dt, th, strong, b, label');
                    for (const el of labelEls) {
                        const t = (el.textContent || '').trim();
                        if (!t) continue;
                        if (/^author(s)?[:\s]/i.test(t) || /^creator(s)?[:\s]/i.test(t) || /\bAuthor(s)?\b/i.test(t) || /\bCreator(s)?\b/i.test(t) || /\bContributor(s)?\b/i.test(t)) {
                            let candidate = el.nextElementSibling || (el.parentElement && el.parentElement.querySelector('dd, td, .value, .displayValue')) || null;
                            if (candidate && candidate.textContent) pushRaw(candidate.textContent);
                            else {
                                const alt = el.parentElement && (el.parentElement.querySelector('.displayCreator, .displayCreators, .recordAuthors, .creator, .authors, .value'));
                                if (alt && alt.textContent) pushRaw(alt.textContent);
                            }
                        }
                    }
                } catch (e) {}

                // Primo / Ex Libris targeted heuristics: look for label/value pairs commonly used
                try {
                    const primoLabels = document.querySelectorAll('.displayFieldLabel, .displayFieldValue, .displayField, .recordDisplay .displayFieldLabel, .recordDisplay .displayField');
                    for (const lbl of primoLabels) {
                        const txt = (lbl.textContent || '').trim();
                        if (!txt) continue;
                        if (/^author(s)?$/i.test(txt) || /^creator(s)?$/i.test(txt) || /\bauthor(s)?\b/i.test(txt)) {
                            // value may be next sibling or within the parent container
                            let valEl = lbl.nextElementSibling || (lbl.parentElement && lbl.parentElement.querySelector('.displayFieldValue, .displayValue, .value, dd, td')) || null;
                            let val = valEl && valEl.textContent ? valEl.textContent.trim() : '';
                            if (!val) {
                                // sometimes the label and value are siblings two levels up
                                const container = lbl.closest('.displayField, .recordDisplay, .record');
                                if (container) {
                                    const v2 = container.querySelector('.displayFieldValue, .value, .displayValue, dd, td');
                                    if (v2 && v2.textContent) val = v2.textContent.trim();
                                }
                            }
                            if (val) pushRaw(val);
                        }
                    }
                } catch (e) {}
            } // end if (!primoExtracted)

            // If on Google Books, filter out noisy metadata blobs (ISBN, publisher, page counts)
            try {
                if (_isGoogleBooks && rawAuthors.length > 0) {
                    rawAuthors = rawAuthors.map(r => (r||'').trim()).filter(Boolean).filter(r => {
                        // Accept if it looks like a person name or explicitly labelled 'By' / 'Author'
                        if (_isLikelyName(r)) return true;
                        if (/\bBy\s+[A-Z]/.test(r) || /\bAuthor\b[:\s]/i.test(r)) return true;
                        return false;
                    });
                }
            } catch (e) {}

            // Normalize to given/family/name
            const seen = new Set();
            const out = [];
            for (let r of rawAuthors) {
                // Remove trailing parenthetical labels like "(Author)" and incidental prefixes
                r = r.replace(/^by\s+/i, '').replace(/\s*\([^\)]*\)\s*$/,'').trim();
                if (!r) continue;
                let given = '';
                let family = '';
                // If comma-separated (Last, First)
                if (r.includes(',')) {
                    const parts = r.split(',').map(p => p.trim()).filter(Boolean);
                    family = parts[0];
                    given = parts.slice(1).join(', ');
                } else {
                    const parts = r.split(/\s+/).filter(Boolean);
                    if (parts.length === 1) family = parts[0];
                    else {
                        family = parts.pop();
                        given = parts.join(' ');
                    }
                }
                const name = given ? `${family}, ${given}` : family;
                if (!seen.has(name)) {
                    seen.add(name);
                    out.push({ given: given, family: family, name: name, type: 'author' });
                }
            }

            // Post-process: remove obviously noisy entries (UI tokens, excessive length)
            const filtered = [];
            for (const c of out) {
                const n = (c.name || '').trim();
                if (!n) continue;
                // drop UI noise
                if (/\b(more|hide|show all|show less|show|Show All|Show Less|hide)\b/i.test(n)) continue;
                // drop excessively long combined blobs
                if (n.length > 120) continue;
                filtered.push(c);
            }

            // Group by normalized family name and pick the best candidate per family
            const famMap = new Map();
            const order = [];
            for (const c of filtered) {
                let family = (c.family || '').trim();
                if (!family) {
                    // derive from name: if 'Last, First' take last before comma, else last word
                    if (c.name && c.name.indexOf(',') >= 0) family = c.name.split(',')[0].trim();
                    else family = (c.name || '').split(/\s+/).slice(-1)[0] || '';
                }
                const famNorm = (family || '').toLowerCase().replace(/[^a-z\p{L}0-9]+/gu,'');
                if (!famNorm) continue;
                if (!famMap.has(famNorm)) {
                    famMap.set(famNorm, c);
                    order.push(famNorm);
                } else {
                    // choose the candidate with more identifying details (prefer more initials/longer given)
                    const existing = famMap.get(famNorm);
                    const score = (x) => ((x.given||'').replace(/[^A-Za-z]/g,'').length) + ((x.name||'').length/100);
                    if (score(c) > score(existing)) famMap.set(famNorm, c);
                }
            }

            // Further deduplicate by signature (letters-only name) to collapse variants
            const sigMap = new Map();
            const final = [];
            for (const f of order) {
                const c = famMap.get(f);
                if (!c) continue;
                const sig = (c.name || '').toLowerCase().replace(/[^a-z\p{L}0-9]+/gu,'');
                if (!sig) continue;
                if (!sigMap.has(sig)) {
                    sigMap.set(sig, c);
                    final.push(c);
                } else {
                    // choose the more informative candidate
                    const existing = sigMap.get(sig);
                    const score = (x) => ((x.given||'').replace(/[^A-Za-z]/g,'').length) + ((x.name||'').length/100);
                    if (score(c) > score(existing)) {
                        // replace in sigMap and in final array
                        sigMap.set(sig, c);
                        const idx = final.indexOf(existing);
                        if (idx >= 0) final[idx] = c;
                    }
                }
            }
            return final;
        }

        normalizeCreatorsFromJSONLD(jsonAuthors) {
                if (!jsonAuthors) return [];
                const list = Array.isArray(jsonAuthors) ? jsonAuthors : [jsonAuthors];
                const out = [];
                for (const a of list) {
                    if (!a) continue;
                    if (typeof a === 'string') {
                        // A string may contain multiple names separated by semicolon, newline, ' and ', or ampersand
                        const parts = a.split(/\s*(?:;|\n|\band\b|&)\s*/i).map(p => p.trim()).filter(Boolean);
                        for (const p of parts) {
                            // If the part contains a comma it's likely "Last, First"; otherwise split on whitespace
                            if (p.includes(',')) {
                                const seg = p.split(',').map(s => s.trim()).filter(Boolean);
                                const family = seg[0];
                                const given = seg.slice(1).join(', ');
                                const name = given ? `${family}, ${given}` : family;
                                out.push({ given: given, family: family, name: name, type: 'author' });
                            } else {
                                const words = p.split(/\s+/).filter(Boolean);
                                if (words.length === 1) {
                                    out.push({ given: '', family: words[0], name: words[0], type: 'author' });
                                } else {
                                    const family = words.pop();
                                    const given = words.join(' ');
                                    out.push({ given: given, family: family, name: `${family}, ${given}`, type: 'author' });
                                }
                            }
                        }
                    } else {
                        out.push({ given: a.given || a.givenName || a.firstName || '', family: a.family || a.familyName || a.lastName || a.surname || '', name: a.name || ((a.family || '') + (a.given ? (', ' + a.given) : '')), type: 'author' });
                    }
                }
                // Deduplicate by normalized name
                const seen = new Set();
                const final = [];
                for (const c of out) {
                    if (!c || !c.name) continue;
                    if (!seen.has(c.name)) {
                        seen.add(c.name);
                        final.push(c);
                    }
                }
                return final;
        }

        // New: basic mapping from JSON-LD/@type or page metadata to itemType
        extractItemType(jsonld) {
            // Prefer explicit JSON-LD @type
            try {
                const scripts = document.querySelectorAll('script[type="application/ld+json"]');
                for (const s of scripts) {
                    try {
                        const j = JSON.parse(s.textContent || '{}');
                        const list = Array.isArray(j) ? j : [j];
                        for (const obj of list) {
                            if (!obj) continue;
                            const t = (obj['@type'] || '').toLowerCase();
                            if (t.includes('book') || t.includes('bookchapter')) return 'book';
                            if (t.includes('scholarlyarticle') || t.includes('article') || t.includes('journalarticle')) return 'journalArticle';
                            if (t.includes('report')) return 'report';
                            if (t.includes('thesis') || t.includes('dissertation')) return 'thesis';
                        }
                    } catch (e) {}
                }
            } catch (e) {}

            // Heuristics from meta tags
            if (document.querySelector('meta[name="citation_journal_title"]') || document.querySelector('meta[name="citation_doi"]')) return 'journalArticle';
            if (document.querySelector('meta[name="citation_isbn"]') || document.querySelector('meta[name="isbn"]')) return 'book';

            // Site/catalog heuristics: inspect labeled fields (Primo / Ex Libris and similar discovery UIs)
            // Many library discovery pages use label/value pairs such as
            // "Resource Type", "Material Type" or plain "Type" whose value may be
            // "Journal Article", "Book", "Book Chapter", etc. Use these cues
            // to prefer more accurate item types when available.
            try {
                const labelNodes = Array.from(document.querySelectorAll('[data-details-label], .displayFieldLabel, dt, th, label, strong, b'));
                for (const lbl of labelNodes) {
                    const labelText = (lbl.textContent || '').trim().toLowerCase();
                    if (!labelText) continue;
                    // Look for labels that indicate a type/format field
                    if (/\b(resource type|material type|type|document type|publication type|format)\b/.test(labelText)) {
                        // find the value element nearby
                        let valueEl = lbl.nextElementSibling || (lbl.parentElement && lbl.parentElement.querySelector('.displayFieldValue, .displayValue, .value, dd, td, .item-details-element-container'));
                        if (!valueEl) {
                            let anc = lbl;
                            for (let i = 0; i < 6 && anc; i++, anc = anc.parentElement) {
                                const cand = anc.querySelector && anc.querySelector('.displayFieldValue, .displayValue, .value, dd, td, .item-details-element-container');
                                if (cand) { valueEl = cand; break; }
                            }
                        }
                        const val = valueEl && valueEl.textContent ? valueEl.textContent.trim().toLowerCase() : '';
                        if (val) {
                            if (/\b(article|journal article|journal)\b/.test(val)) return 'journalArticle';
                            if (/\b(book|monograph)\b/.test(val)) return 'book';
                            if (/\b(chapter|book section)\b/.test(val)) return 'bookSection';
                            if (/\b(thesis|dissertation)\b/.test(val)) return 'thesis';
                            if (/\b(report|technical report|tech report)\b/.test(val)) return 'report';
                        }
                    }
                }
            } catch (e) {}

            // default
            return 'webpage';
        }

        extractDate() {
            const selectors = [
                'meta[name="citation_publication_date"]',
                'meta[name="citation_date"]',
                'meta[property="article:published_time"]',
                'meta[name="date"]',
                'meta[name="dc.date"]',
                'time[datetime]',
                '.date, .publication-date'
            ];
            
            for (const selector of selectors) {
                const element = document.querySelector(selector);
                if (element) {
                    let dateStr = element.getAttribute('content') 
                                 || element.getAttribute('datetime') 
                                 || element.textContent;
                    
                    if (dateStr) {
                        const date = new Date(dateStr);
                        if (!isNaN(date.getTime())) {
                            return date.getFullYear().toString();
                        }
                    }
                }
            }

            // Extra Leanpub-specific checks: prefer explicit meta/itemprop/data attributes
            try {
                const hostLp = (location.host || '').toLowerCase();
                if (hostLp.includes('leanpub')) {
                    const leanpubSelectors = [
                        'meta[itemprop="datePublished"]', '[itemprop="datePublished"]',
                        '[data-publish-date]', '[data-published]', '[data-release-date]',
                        'meta[name="release_date"]', 'meta[name="publish_date"]', 'meta[name="book:release_date"]'
                    ];
                    for (const sel of leanpubSelectors) {
                        const el = document.querySelector(sel);
                        if (!el) continue;
                        const txt = el.getAttribute && el.getAttribute('content') ? el.getAttribute('content') : (el.textContent || '');
                        if (!txt) continue;
                        const y = (txt.match(/(?:19|20)\d{2}/) || [])[0];
                        if (y) return y;
                    }
                }
            } catch (e) {}
            // Search label/value pairs (Primo and other catalogs) for publication/year info
            try {
                const labelSel = ['dt','th','strong','b','label','.displayFieldLabel','.displayField'].join(',');
                for (const el of document.querySelectorAll(labelSel)) {
                    const t = (el.textContent || '').trim();
                    if (!t) continue;
                    if (/\b(publication|published|publication date|date|year|creation|creation date)\b/i.test(t)) {
                        let val = '';
                        if (el.nextElementSibling && el.nextElementSibling.textContent) val = el.nextElementSibling.textContent.trim();
                        else if (el.parentElement) {
                            const v = el.parentElement.querySelector('.displayFieldValue, .displayValue, .value, dd, td, .detailValue, span');
                            if (v && v.textContent) val = v.textContent.trim();
                        }
                        if (val) {
                                // Year formats: 4-digit, or textual dates (e.g., January 1, 1992),
                                // or common numeric dates like DD/MM/YYYY or DD-MM-YYYY
                                const mY = val.match(/(?:19|20)\d{2}/);
                                if (mY) return mY[0];
                                const m2 = val.match(/([A-Za-z]+\s+\d{1,2},?\s*(?:19|20)\d{2})/);
                                if (m2) {
                                    const d = new Date(m2[0]);
                                    if (!isNaN(d.getTime())) return d.getFullYear().toString();
                                }
                                const mDate = val.match(/(\d{1,2}[\/\-]\d{1,2}[\/\-](?:19|20)\d{2})/);
                                if (mDate) {
                                    const parts = mDate[1].split(/[\/\-]/).map(p=>parseInt(p,10));
                                    const y = parts[2];
                                    if (y && y >= 1000 && y <= 9999) return String(y);
                                }
                        }
                    }
                }
            } catch (e) {}

            // Amazon / product detail containers: look for publication date or any 4-digit year
            try {
                const detailSelectors = ['#detailBullets_feature_div','#productDetails_detailBullets_sections1','#productDetailsTable','#prodDetails','#bookDetails_feature_div'];
                for (const sel of detailSelectors) {
                    const el = document.querySelector(sel);
                    if (!el || !el.textContent) continue;
                    const txt = el.textContent;
                    const m = txt.match(/Publication\s*Date[:\s]*([A-Za-z0-9,\s]+\d{4})/i) || txt.match(/(?:19|20)\d{2}/);
                    if (m) {
                        // m[1] may be a full date string (e.g., "January 1, 1992")
                        const cand = m[1] || m[0];
                        const d = new Date(cand);
                        if (!isNaN(d.getTime())) return d.getFullYear().toString();
                        const y = (cand || '').match(/(?:19|20)\d{2}/);
                        if (y) return y[0];
                    }
                }
            } catch (e) {}
            // Google Books specific: header often contains "By Author · 2022" — extract year
            try {
                const host = (location.host || '').toLowerCase();
                if (host.includes('books.google') || /\/books\//i.test(location.pathname)) {
                    const headerNodes = Array.from(document.querySelectorAll('div.RQZ6xb, div.hVh9rf, header'));
                    const headerText = headerNodes.map(n => (n.textContent || '')).join(' ');
                    const topText = (headerText && headerText.length > 0) ? headerText : (document.body.textContent || '').slice(0, 1200);
                    // Try patterns like "By NAME · 2022" or any nearby 4-digit year
                    let m = topText.match(/By\s+[^·•\u2022\n\r]+?[·•\u2022]\s*(\d{4})/i);
                    if (m && m[1]) return m[1];
                    const m2 = topText.slice(0, 500).match(/(?:19|20)\d{2}/);
                    if (m2) return m2[0];
                }
            } catch (e) {}
            // Leanpub specific: look for published/release date blocks (conservative)
            try {
                const hostLp = (location.host || '').toLowerCase();
                if (hostLp.includes('leanpub')) {
                    const lpSelectors = ['.book-meta', '.book-info', '.book-details', '.bookPage', '.book-heading', '.book-meta .published', '.product-info', '.publish-info'];
                    for (const sel of lpSelectors) {
                        const el = document.querySelector(sel);
                        if (!el || !el.textContent) continue;
                        const txt = el.textContent;
                        // Only accept a loose 4-digit year when the surrounding text indicates a publish/release context
                        const hasPublishHint = /\b(publish|published|release|released|release date|published by|copyright|©)\b/i.test(txt) || /publish|release|product-info|book-meta|publish-info/i.test(sel);
                        if (hasPublishHint) {
                            // Try to find a 4-digit year first
                            const y = (txt.match(/(?:19|20)\d{2}/) || [])[0];
                            if (y) return y;
                        }
                        // Try to find 'Published' followed by a date string
                        const m = txt.match(/Published[:\s]*([A-Za-z0-9,\s]+(?:19|20)\d{2})/i) || txt.match(/Release\s*date[:\s]*([A-Za-z0-9,\s]+(?:19|20)\d{2})/i);
                        if (m && m[1]) {
                            const d = new Date(m[1].trim());
                            if (!isNaN(d.getTime())) return d.getFullYear().toString();
                            const yy = (m[1].match(/(?:19|20)\d{2}/) || [])[0];
                            if (yy) return yy;
                        }
                    }
                    // Fallback: search document body for 'Published' near a year
                    const bodyText = document.body && document.body.textContent ? document.body.textContent : '';
                    const bf = bodyText.match(/Published[:\s]*([A-Za-z0-9,\s]+(?:19|20)\d{2})/i) || bodyText.match(/Release\s*date[:\s]*([A-Za-z0-9,\s]+(?:19|20)\d{2})/i);
                    if (bf && bf[1]) {
                        const d = new Date(bf[1].trim());
                        if (!isNaN(d.getTime())) return d.getFullYear().toString();
                        const yy = (bf[1].match(/(?:19|20)\d{2}/) || [])[0];
                        if (yy) return yy;
                    }
                }
            } catch (e) {}
            // Global fallback: capture copyright/creation tokens like "c1995", "c.1995" or "(c1995)"
            try {
                const body = document.body && document.body.textContent ? document.body.textContent : '';
                const mC = body.match(/c\.?\s*(?:19|20)\d{2}/i);
                if (mC) {
                    const y = (mC[0].match(/(?:19|20)\d{2}/) || [])[0];
                    if (y) return y;
                }
            } catch (e) {}
            // Additional global copyright/year forms (© 2025, Copyright 2025, (c)2025)
            try {
                const body2 = document.body && document.body.textContent ? document.body.textContent : '';
                const copyMatch = body2.match(/©\s*(?:19|20)\d{2}/) || body2.match(/Copyright\s*(?:©)?\s*(?:19|20)\d{2}/i) || body2.match(/\(c\)\s*(?:19|20)\d{2}/i);
                if (copyMatch) {
                    const yy = (copyMatch[0].match(/(?:19|20)\d{2}/) || [])[0];
                    if (yy) return yy;
                }
            } catch (e) {}

            // Extra Leanpub-specific checks: data attributes, itemprop, and meta tags
            try {
                const hostLp = (location.host || '').toLowerCase();
                if (hostLp.includes('leanpub')) {
                    const leanpubSelectors = [
                        'meta[itemprop="datePublished"]', '[itemprop="datePublished"]',
                        '[data-publish-date]', '[data-published]', '[data-release-date]',
                        'meta[name="release_date"]', 'meta[name="publish_date"]', 'meta[name="book:release_date"]'
                    ];
                    for (const sel of leanpubSelectors) {
                        const el = document.querySelector(sel);
                        if (!el) continue;
                        const txt = el.getAttribute && el.getAttribute('content') ? el.getAttribute('content') : (el.textContent || '');
                        if (!txt) continue;
                        const y = (txt.match(/(?:19|20)\d{2}/) || [])[0];
                        if (y) return y;
                    }

                    // Also check the H1's nearby DOM for a 'Published' token and year
                    const h1 = document.querySelector('h1, h1.book-title, h1[itemprop="name"]');
                    if (h1) {
                        // search siblings and parent nodes (small neighborhood)
                        let node = h1.parentElement;
                        for (let depth = 0; node && depth < 5; depth++, node = node.parentElement) {
                            const txt = node.textContent || '';
                            const m = txt.match(/Published[:\s]*([A-Za-z0-9,\s]+(?:19|20)\d{2})/i) || txt.match(/(?:19|20)\d{2}/);
                            if (m && m[1]) {
                                const d = new Date(m[1].trim());
                                if (!isNaN(d.getTime())) return d.getFullYear().toString();
                                const yy = (m[1].match(/(?:19|20)\d{2}/) || [])[0];
                                if (yy) return yy;
                            } else if (m && m[0]) {
                                const yy = (m[0].match(/(?:19|20)\d{2}/) || [])[0];
                                if (yy) return yy;
                            }
                        }
                    }
                }
            } catch (e) {}
            // Try common visible labels (e.g. "Published: 2019")
            const textMatch = document.body.textContent.match(/Published[:\s]*([0-9]{4})/i) || document.body.textContent.match(/Publication Date[:\s]*([0-9]{4})/i);
            if (textMatch) return textMatch[1];
            return '';
        }
        
        findDOI() {
            // Look for DOI in meta tags
            const doiSelectors = [
                'meta[name="citation_doi"]',
                'meta[name="DOI"]',
                'meta[name="dc.identifier"]',
                'meta[name="dc.Identifier"]',
                'meta[name="dc.identifier"][content*="doi:"]',
                'meta[name="bepress_citation_doi"]'
            ];
            for (const selector of doiSelectors) {
                const element = document.querySelector(selector);
                if (element) {
                    let doi = element.getAttribute('content') || element.getAttribute('value') || element.textContent;
                    if (doi) {
                        doi = doi.replace(/^(doi:|https?:\/\/(dx\.)?doi\.org\/)/i, '').trim();
                        const m = doi.match(/(10\.\d{4,9}\/\S+)/);
                        if (m) return m[1];
                        if (doi.match(/^10\./)) return doi;
                    }
                }
            }

            // Look for DOI in page text or anchor hrefs (doi.org links)
            const doiRegex = /10\.\d{4,9}\/[^\s\)\]\"']+/;
            // First, check Primo/labelled identifier fields which often contain the DOI
            try {
                const idLabels = Array.from(document.querySelectorAll('[data-details-label], .displayFieldLabel, dt, th, label'));
                for (const lbl of idLabels) {
                    const t = (lbl.textContent || '').trim();
                    if (!t) continue;
                    if (/\b(identifier|doi|DOI)\b/i.test(t)) {
                        // look for DOI-like tokens in the sibling/value container
                        let container = lbl.nextElementSibling || lbl.parentElement;
                        if (container) {
                            const txt = (container.textContent || '') + ' ' + (lbl.parentElement && lbl.parentElement.textContent || '');
                            const m = txt.match(doiRegex);
                            if (m) return m[0];
                        }
                    }
                }
            } catch (e) {}
            const textMatch = document.body.textContent.match(doiRegex);
            if (textMatch) return textMatch[0];
            // Links
            const anchors = document.querySelectorAll('a[href*="doi.org"], a[href*="/doi/"]');
            for (const a of anchors) {
                const href = a.getAttribute('href') || '';
                const m = href.match(/10\.\d{4,9}\/[^\s\)\]\"']+/);
                if (m) return m[0];
            }
            return '';
        }

        // Try to find ISBN on the page (meta tags, visible text, or links)
        findISBN() {
            const metaSelectors = ['meta[name="citation_isbn"]','meta[name="isbn"]','meta[property="books:isbn"]','meta[name="book_isbn"]'];
            for (const sel of metaSelectors) {
                const el = document.querySelector(sel);
                if (el) {
                    const c = el.getAttribute('content') || el.textContent || '';
                    const norm = (c || '').replace(/[^0-9Xx]/g,'');
                    if (/^(97[89]\d{10}|\d{9}[0-9Xx])$/.test(norm)) return norm;
                }
            }

            // Amazon specific: check #ASIN input or data-asin attributes
            try {
                const asinInput = document.getElementById('ASIN');
                if (asinInput && asinInput.value) {
                    const cand = (asinInput.value || '').replace(/[^0-9Xx]/g,'');
                    if (/^(97[89]\d{10}|\d{9}[0-9Xx])$/.test(cand)) return cand;
                }
            } catch (e) {}
            try {
                const dataAsin = document.querySelector('[data-asin]');
                if (dataAsin) {
                    const cand = (dataAsin.getAttribute('data-asin')||'').replace(/[^0-9Xx]/g,'');
                    if (/^(97[89]\d{10}|\d{9}[0-9Xx])$/.test(cand)) return cand;
                }
            } catch (e) {}

            // Search common product detail containers for 'ISBN' labels
            const detailSelectors = [
                '#detailBullets_feature_div',
                '#productDetails_detailBullets_sections1',
                '#productDetailsTable',
                '#bookDetails_feature_div',
                '#prodDetails'
            ];
            for (const sel of detailSelectors) {
                const el = document.querySelector(sel);
                if (el && el.textContent) {
                    const m = el.textContent.match(/ISBN(?:-13)?:?\s*([0-9\-Xx]{10,17})/i) || el.textContent.match(/ISBN(?:-10)?:?\s*([0-9\-Xx]{10,17})/i);
                    if (m) {
                        const norm = m[1].replace(/[^0-9Xx]/g,'');
                        if (/^(97[89]\d{10}|\d{9}[0-9Xx])$/.test(norm)) return norm;
                    }
                }
            }

            // Generic: look for ISBN in page text (prefer ISBN-13)
            const isbn13 = document.body.textContent.match(/(97[89][0-9\-]{10,})/g);
            if (isbn13 && isbn13.length) {
                for (const candidate of isbn13) {
                    const norm = candidate.replace(/[^0-9Xx]/g,'');
                    if (/^97[89]\d{10}$/.test(norm)) return norm;
                }
            }

            const isbn10 = document.body.textContent.match(/\b\d{9}[0-9Xx]\b/g);
            if (isbn10 && isbn10.length) {
                for (const candidate of isbn10) {
                    const norm = candidate.replace(/[^0-9Xx]/g,'');
                    if (/^\d{9}[0-9Xx]$/.test(norm)) return norm;
                }
            }

            // Fallback: check links for /dp/ASIN or /gp/product/
            const anchors = document.querySelectorAll('a[href]');
            for (const a of anchors) {
                const href = a.getAttribute('href') || '';
                const m = href.match(/\/dp\/([A-Z0-9]{9,12})|\/gp\/product\/([A-Z0-9]{9,12})/i);
                if (m) {
                    const cand = (m[1]||m[2]||'').replace(/[^0-9Xx]/g,'');
                    if (/^(97[89]\d{10}|\d{9}[0-9Xx])$/.test(cand)) return cand;
                }
            }

            return '';
        }
        
        extractJournalTitle() {
            const selectors = [
                'meta[name="citation_journal_title"]',
                'meta[name="prism.publicationName"]',
                'meta[property="og:site_name"]'
            ];
            
            for (const selector of selectors) {
                const element = document.querySelector(selector);
                if (element) {
                    const title = element.getAttribute('content');
                    if (title && title.trim()) {
                        return title.trim();
                    }
                }
            }
            return '';
        }
        
        extractAbstract() {
            const selectors = [
                'meta[name="description"]',
                'meta[name="citation_abstract"]',
                'meta[property="og:description"]',
                '.abstract',
                '#abstract'
            ];
            
            for (const selector of selectors) {
                const element = document.querySelector(selector);
                if (element) {
                    const content = element.getAttribute('content') || element.textContent;
                    if (content && content.trim() && content.length > 50) {
                        return content.trim().substring(0, 500) + (content.length > 500 ? '...' : '');
                    }
                }
            }
            return '';
        }

        // Extract JSON-LD structured data (application/ld+json)
        extractJSONLD() {
            const scripts = document.querySelectorAll('script[type="application/ld+json"]');
            for (const s of scripts) {
                try {
                    const j = JSON.parse(s.textContent || '{}');
                    // j can be an array or object; normalize
                    const list = Array.isArray(j) ? j : [j];
                    for (const obj of list) {
                        if (!obj) continue;
                        const t = (obj['@type'] || '').toLowerCase();
                        if (t.includes('book') || t.includes('creativework') || t.includes('article') || t.includes('scholarlyarticle') || t.includes('webpage')) {
                            const out = {};
                            if (obj.name) out.title = obj.name;
                            if (obj.headline) out.title = out.title || obj.headline;
                            // Check several possible author/creator shapes in JSON-LD
                            const pickAuthorsFrom = (source) => {
                                if (!source) return [];
                                if (Array.isArray(source)) return source.map(a => (a && (a.name || a))).filter(Boolean);
                                return [(source && (source.name || source))].filter(Boolean);
                            };
                            if (obj.author) out.authors = pickAuthorsFrom(obj.author);
                            else if (obj.creator) out.authors = pickAuthorsFrom(obj.creator);
                            else if (obj.mainEntity && obj.mainEntity.creator) out.authors = pickAuthorsFrom(obj.mainEntity.creator);
                            if (obj.datePublished) out.date = obj.datePublished;
                            if (obj.url) out.url = obj.url;
                            if (obj.description) out.abstract = obj.description;
                            return out;
                        }
                    }
                } catch (e) {
                    // ignore parse errors
                }
            }
            return {};
        }

        // Improved title extraction that prefers structured data and citation meta,
        // cleans common site suffixes, and returns a trimmed title suitable for BibTeX.
        extractTitle(jsonld, metaTitle) {
            let t = '';
            if (jsonld && (jsonld.title || jsonld.headline)) t = jsonld.title || jsonld.headline;
            else if (metaTitle) t = metaTitle;
            else t = document.title || '';

            t = t.trim();
            // Remove trailing site names after separators (e.g., " - Site", " | Site", ": Site")
            t = t.replace(/\s*[-|:\u2014]\s*[^-\|:\u2014]{1,100}$/, '');
            // Remove common suffixes like " — SiteName"
            t = t.replace(/\s*—\s*[^\-\|:\u2014]{1,100}$/, '');
            // Remove surrounding quotation marks
            t = t.replace(/^\"|\"$/g, '').replace(/^\'|\'$/g, '');
            // Apply existing cleaning heuristics
            t = this.cleanTitle(t);

            // Site-specific fixes
            try {
                const host = (location.host || '').toLowerCase();
                // Leanpub: prefer the visible H1 title and strip currency/price tokens
                if (host.includes('leanpub')) {
                    // Prefer more robust selectors for Leanpub title and avoid UI headings like "About the Book"
                    const titleSelectors = ['h1.book-title','h1[itemprop="name"]','h1','.book-title','.book-heading h1','.book-info h1','.book-page h1'];
                    let candidate = '';
                    for (const sel of titleSelectors) {
                        const el = document.querySelector(sel);
                        if (el && el.textContent) {
                            const txt = el.textContent.trim();
                            if (!txt) continue;
                            if (/^about\s+the\s+book$/i.test(txt)) continue; // skip UI section headings
                            candidate = txt;
                            break;
                        }
                    }
                    // Fallback to meta tags if visible title is not helpful
                    if (!candidate) {
                        const og = this.getMeta(['meta[property="og:title"]','meta[name="twitter:title"]','meta[name="citation_title"]']);
                        if (og && !/^about\s+the\s+book$/i.test(og)) candidate = og;
                    }
                    if (candidate) {
                        // remove trailing price tokens like "$3.50", "USD $3.50", "£3.50", etc.
                        candidate = candidate.replace(/\s*(?:and\s*)?(?:\$|USD\s*\$|£|GBP|€|EUR)\s*[0-9]{1,3}(?:[\.,][0-9]{1,2})?\b/gi, '').trim();
                        candidate = this.cleanTitle(candidate);
                        if (candidate) t = candidate;
                    } else {
                        // if meta/title included price or author, remove currency suffixes like "and $3.50"
                        t = t.replace(/\s*(?:and\s*)?(?:\$|USD\s*\$|£|GBP|€|EUR)\s*[0-9]{1,3}(?:[\.,][0-9]{1,2})?\b/gi, '').trim();
                    }
                }
                // Primo / Ex Libris: strip institution suffixes like " - University of Surrey /"
                if (/primo\.exlibrisgroup|surrey\.primo|discovery\/fulldisplay/i.test(location.href) || host.includes('primo')) {
                    // Remove trailing " - Some Institution /" or " - Some Institution" fragments
                    t = t.replace(/\s*-\s*[^-\/]{1,120}\s*(?:\/\s*)?$/i, '').trim();
                    // Also remove trailing institutional prefixes separated by a slash
                    t = t.replace(/\s*\/\s*[^\/]{1,80}\s*$/i, '').trim();
                }
                // Generic: strip trailing currency tokens from titles
                t = t.replace(/\s*(?:\$|USD\s*\$)\s*\d+(?:\.\d+)?\b/i, '').trim();
            } catch (e) {}
            return t;
        }

        // Map internal itemType and page metadata to a BibTeX entry type (per bibtex-formats.md)
        mapToBibtexType(itemType, meta = {}) {
            // Prefer explicit cues
            const { journal, isbn, publisher } = meta;
            if (itemType === 'journalArticle' || /journal/i.test(journal || '')) return 'article';
            if (itemType === 'book' || isbn) return 'book';
            if (itemType === 'bookSection' || itemType === 'chapter' || /chapter/i.test(itemType)) return 'incollection';
            // For theses, try to distinguish level by degree clues
            if (itemType === 'thesis' || /thesis|dissertation/i.test(itemType)) {
                // If page contains degree clues, prefer phdthesis or mastersthesis
                const degreeMeta = this.getMeta(['meta[name="citation_degree"]','meta[name="degree"]','meta[name="citation_thesis_type"]']);
                if (degreeMeta && /phd|doctor/i.test(degreeMeta)) return 'phdthesis';
                if (degreeMeta && /master|msc|ma|ms/i.test(degreeMeta)) return 'mastersthesis';
                // fallback to phd if document body mentions 'PhD' near top
                if (/\bPhD\b|\bDoctoral\b/i.test(document.body.textContent.slice(0,2000))) return 'phdthesis';
                return 'mastersthesis';
            }
            if (itemType === 'report' || itemType === 'techreport') return 'techreport';
            if (itemType === 'conferencePaper' || /conference|proceedings|inproceedings/i.test(itemType)) return 'inproceedings';
            if (itemType === 'webpage') return 'misc';
            // Default to misc when unsure
            return 'misc';
        }

        // Extract book-specific fields useful for BibTeX (@book, @incollection)
        extractBookFields() {
            const publisher = this.getMeta(['meta[name="citation_publisher"]','meta[name="publisher"]','meta[property="og:site_name"]']) || '';
            const address = this.getMeta(['meta[name="citation_publisher_place"]','meta[name="citation_place"]','meta[name="citation_city"]','meta[name="DC.publisher-location"]']) || '';
            const isbn = this.getMeta(['meta[name="citation_isbn"]','meta[name="isbn"]']) || '';
            const edition = this.getMeta(['meta[name="citation_edition"]','meta[name="edition"]']) || '';
            return { publisher, address, isbn, edition };
        }

        // Extract thesis-specific fields (school/institution, degree)
        extractThesisInfo() {
            const school = this.getMeta(['meta[name="citation_school"]','meta[name="citation_dissertation_institution"]','meta[name="DC.source"]']) || '';
            const degree = this.getMeta(['meta[name="citation_degree"]','meta[name="degree"]','meta[name="citation_thesis_type"]']) || '';
            return { school, degree };
        }

        // Extract article-specific fields (journal, volume, number/issue, pages, issn)
        extractArticleFields() {
            const journal = this.getMeta(['meta[name="citation_journal_title"]','meta[name="prism.publicationName"]']) || this.extractJournalTitle() || '';
            const volume = this.getMeta(['meta[name="citation_volume"]','meta[name="prism.volume"]']) || '';
            const number = this.getMeta(['meta[name="citation_issue"]','meta[name="prism.number"]','meta[name="citation_number"]']) || '';
            const firstPage = this.getMeta(['meta[name="citation_firstpage"]','meta[name="prism.pageStart"]']) || '';
            const lastPage = this.getMeta(['meta[name="citation_lastpage"]','meta[name="prism.pageEnd"]']) || '';
            const pages = firstPage && lastPage ? (firstPage + '--' + lastPage) : (firstPage || lastPage || '');
            const issn = this.getMeta(['meta[name="citation_issn"]','meta[name="issn"]']) || '';
            return { journal, volume, number, pages, issn };
        }

        // Build a BibTeX-friendly object from extracted data
        buildBibtexObject(extracted) {
            const type = extracted.bibtexType || 'misc';
            // Generate cite key: LastnameYearShortTitle
            const authorFamily = (extracted.creators && extracted.creators[0] && extracted.creators[0].family) ? extracted.creators[0].family : 'anon';
            const year = (extracted.year && extracted.year.match(/^\d{4}$/)) ? extracted.year : (new Date().getFullYear()).toString();
            const shortTitle = (extracted.title || '').split(/\s+/).slice(0,4).join(' ');
            const citeKey = this.slugify(authorFamily) + year + this.slugify(shortTitle).slice(0,10);

            // Common fields mapping
            const authors = (extracted.creators && extracted.creators.length) ? extracted.creators.map(c => c.name).join(' and ') : (extracted.authors || '');
            const fields = {};
            if (authors) fields.author = authors;
            if (extracted.title) fields.title = extracted.title;
            if (extracted.year) fields.year = extracted.year;
            if (extracted.doi) fields.doi = extracted.doi;
            if (extracted.abstract) fields.note = extracted.abstract;

            // Type-specific fields
            switch (type) {
                case 'article':
                    if (extracted.bib && extracted.bib.journal) fields.journal = extracted.bib.journal;
                    if (extracted.bib && extracted.bib.volume) fields.volume = extracted.bib.volume;
                    if (extracted.bib && extracted.bib.number) fields.number = extracted.bib.number;
                    if (extracted.bib && extracted.bib.pages) fields.pages = extracted.bib.pages;
                    if (extracted.bib && extracted.bib.issn) fields.issn = extracted.bib.issn;
                    break;
                case 'book':
                    if (extracted.bib && extracted.bib.publisher) fields.publisher = extracted.bib.publisher || extracted.publisher;
                    if (extracted.bib && extracted.bib.address) fields.address = extracted.bib.address;
                    if (extracted.bib && extracted.bib.edition) fields.edition = extracted.bib.edition;
                    if (extracted.isbn) fields.isbn = extracted.isbn;
                    break;
                case 'phdthesis':
                case 'mastersthesis':
                    if (extracted.bib && extracted.bib.school) fields.school = extracted.bib.school || extracted.bib.school || '';
                    if (extracted.bib && extracted.bib.degree) fields.type = extracted.bib.degree || '';
                    break;
                case 'inproceedings':
                case 'incollection':
                    if (extracted.bib && extracted.bib.booktitle) fields.booktitle = extracted.bib.booktitle || '';
                    if (extracted.bib && extracted.bib.publisher) fields.publisher = extracted.bib.publisher || '';
                    break;
                default:
                    // misc and fallback fields
                    if (extracted.url) fields.howpublished = `{\\url{${extracted.url}}}`;
            }

            // Add some common optional fields
            if (extracted.journal && !fields.journal) fields.journal = extracted.journal;
            if (extracted.pages && !fields.pages) fields.pages = extracted.pages.replace(/-/g,'--');
            if (extracted.publisher && !fields.publisher) fields.publisher = extracted.publisher;

            return { entryType: type, citeKey: citeKey, fields };
        }

        // Convert bibtexObject into a BibTeX string
        buildBibtexString(bibObj) {
            if (!bibObj) return '';
            const esc = (v) => String(v).replace(/\n/g,' ').replace(/\s+/g,' ').trim();
            const lines = [];
            lines.push(`@${bibObj.entryType}{${bibObj.citeKey},`);
            for (const [k, v] of Object.entries(bibObj.fields)) {
                if (v === undefined || v === null || v === '') continue;
                // Wrap values in braces
                lines.push(`  ${k} = {${esc(v)}} ,`);
            }
            // Remove trailing comma on last field
            if (lines.length > 1) {
                const last = lines.pop();
                const trimmed = last.replace(/,\s*$/, '');
                lines.push(trimmed);
            }
            lines.push('}');
            return lines.join('\n');
        }

        slugify(s) {
            return String(s || '').toLowerCase().replace(/[^a-z0-9]+/g,'').replace(/^\d+/, '').slice(0, 20);
        }

        // Utility: get first meta content from selectors
        getMeta(selectors) {
            for (const sel of selectors) {
                const el = document.querySelector(sel);
                if (el) {
                    const c = el.getAttribute('content') || el.textContent;
                    if (c && c.trim()) return c.trim();
                }
            }
            return '';
        }

        // Clean noisy titles often injected by site UI
        cleanTitle(s) {
            if (!s) return s;
            // Remove trailing bracketed labels like [PDF/iPad/Kindle]
            s = s.replace(/\s*\[[^\]]+\]\s*$/,'');
            // Remove trailing " - Author" or " by Author" at end
            s = s.replace(/\s+by\s+[^-\|\[]+$/i, '');
            s = s.replace(/\s+-\s+[^\|\[]+$/i, '');
            // Trim and normalize whitespace
            return s.replace(/\s+/g,' ').trim();
        }
        
        getBestTranslator() {
            return this.translators[0] || null;
        }
    }
    
    // Create translator instance
    const translator = new BelloTranslator();
    // Expose a small debug/test API on window so pages can invoke the extractor from the console
    try {
        if (!window.belloConnector) {
            window.belloConnector = {};
        }
        // Keep the content-script's API promise-based: always return a Promise
        window.belloConnector.extractWebPage = function() {
            try {
                // translator.extractWebPage() is synchronous in this context; wrap in Promise
                return Promise.resolve(translator.extractWebPage());
            } catch (e) {
                return Promise.resolve({ error: e && e.message ? e.message : String(e) });
            }
        };
        // synchronous accessor for last cached extraction (if any)
        window.belloConnector.getLastExtract = function() {
            return window.belloConnector.lastExtract || window.belloConnector._lastExtract || null;
        };
        window.belloConnector.getTranslators = function() {
            try {
                translator.detectTranslators();
                return translator.translators.map(t => ({ id: t.id, name: t.name, priority: t.priority }));
            } catch (e) { return []; }
        };
        // Fallback: respond to CustomEvent requests from page context. This allows
        // console/page scripts to request extraction even if direct `window.belloConnector`
        // access is prevented by the page (CSP/isolated worlds). Usage from console:
        // window.addEventListener('bello-response-extract', e => console.log(e.detail));
        // window.dispatchEvent(new CustomEvent('bello-request-extract'));
        window.addEventListener('bello-request-extract', function(_ev) {
            try {
                const data = translator.extractWebPage();
                // Ensure the event detail is a plain, structured-cloneable object
                let safeData;
                try {
                    safeData = JSON.parse(JSON.stringify(data));
                } catch (e) {
                    // fallback: copy only top-level primitive fields
                    safeData = {};
                    try {
                        for (const k of Object.keys(data || {})) {
                            const v = data[k];
                            if (v === null) safeData[k] = null;
                            else if (typeof v === 'object') safeData[k] = JSON.parse(JSON.stringify(v || {}));
                            else safeData[k] = v;
                        }
                    } catch (e2) { safeData = { error: 'unserializable-data' }; }
                }
                // Also include a JSON string and postMessage for page-context safety
                const jsonStr = (function(){ try { return JSON.stringify(safeData); } catch(e){ return ''; } })();
                try {
                    window.dispatchEvent(new CustomEvent('bello-response-extract', { detail: { success: true, data: safeData, json: jsonStr } }));
                } catch (e) {
                    // If dispatching CustomEvent with object detail is blocked, send only the JSON string
                    try { window.dispatchEvent(new CustomEvent('bello-response-extract', { detail: { success: true, json: jsonStr } })); } catch(e2) {}
                }
                // Post a message which uses structured-clone and is safe across contexts
                try { window.postMessage({ source: 'bello-connector', type: 'response-extract', data: safeData }, '*'); } catch (e) {}
            } catch (err) {
                window.dispatchEvent(new CustomEvent('bello-response-extract', { detail: { success: false, error: err && err.message ? err.message : String(err) } }));
            }
        }, false);
        // Allow page scripts to request attachment discovery via CustomEvent
        // (This mirrors the chrome.runtime.onMessage 'findAttachments' handler for use by page scripts)
        window.addEventListener('bello-request-findAttachments', function(_ev) {
            try {
                const results = [];
                // meta tags commonly used by publishers
                const metaSelectors = [
                    'meta[name="citation_pdf_url"]',
                    'meta[name="citation_fulltext_pdf"]',
                    'meta[name="citation_fulltext_html_url"]',
                    'meta[name="pdf_url"]',
                    'link[type="application/pdf"]',
                    'meta[property="og:pdf"]'
                ];
                for (const sel of metaSelectors) {
                    document.querySelectorAll(sel).forEach(el => {
                        const v = el.getAttribute('content') || el.getAttribute('href') || el.getAttribute('src') || '';
                        if (v && !results.includes(v)) results.push(v);
                    });
                }

                // Primo / Ex Libris specific detection
                const isPrimoPage = /primo|exlibris|discovery\/fulldisplay/i.test(location.href) || 
                                    document.querySelector('prm-full-view, prm-full-view-service-container, [class*="primo"]') !== null;
                
                if (isPrimoPage) {
                    // Look for Primo service containers with links
                    const primoServiceSelectors = [
                        'prm-full-view-service-container a[href]',
                        'prm-service-button a[href]',
                        'prm-view-online a[href]',
                        'prm-alma-viewit a[href]',
                        '.service-type a[href]',
                        '.getit-link a[href]',
                        '[class*="viewit"] a[href]',
                        '[class*="full-text"] a[href]'
                    ];
                    
                    for (const sel of primoServiceSelectors) {
                        try {
                            document.querySelectorAll(sel).forEach(a => {
                                const href = (a.getAttribute('href') || '').trim();
                                if (!href || href === '#') return;
                                const absHref = (() => { try { return new URL(href, location.href).toString(); } catch(e) { return href; } })();
                                if (!results.includes(absHref)) results.push(absHref);
                            });
                        } catch (e) {}
                    }
                    
                    // Look for OA/FullText buttons
                    const oaKeywords = /\b(open access|view online|online access|full text|fulltext|view pdf|download pdf)\b/i;
                    document.querySelectorAll('a[href], button[onclick], [role="button"]').forEach(el => {
                        try {
                            const text = (el.textContent || '').trim();
                            if (oaKeywords.test(text) || oaKeywords.test(el.getAttribute('aria-label') || '')) {
                                let href = el.getAttribute('href') || el.getAttribute('data-url') || '';
                                if (!href || href === '#') {
                                    const parentA = el.closest('a[href]');
                                    if (parentA) href = parentA.getAttribute('href') || '';
                                }
                                if (href && href !== '#') {
                                    const absHref = (() => { try { return new URL(href, location.href).toString(); } catch(e) { return href; } })();
                                    if (!results.includes(absHref)) results.push(absHref);
                                }
                            }
                        } catch (e) {}
                    });
                }

                // Resolver patterns
                const resolverPatterns = [
                    /libkey\.(io|org|link)/i, /unpaywall\.org/i, /oadoi\.org/i, /doaj\.org/i,
                    /core\.ac\.uk/i, /europepmc\.org/i, /arxiv\.org/i, /ssrn\.com/i
                ];

                document.querySelectorAll('a[href]').forEach(a => {
                    try {
                        const href = (a.getAttribute('href')||'').trim();
                        if (!href || href === '#' || href.startsWith('javascript:')) return;
                        const absHref = (() => { try { return new URL(href, location.href).toString(); } catch(e) { return href; } })();
                        const lower = href.split('?')[0].toLowerCase();
                        const text = (a.textContent||'').trim().toLowerCase();
                        const cls = (a.className||'').toLowerCase();
                        
                        if (lower.endsWith('.pdf') || /\.pdf([?#]|$)/i.test(href)) {
                            if (!results.includes(absHref)) results.push(absHref);
                            return;
                        }
                        if (/\b(pdf|full text|download|fulltext|view online|view pdf|open pdf|online access)\b/i.test(text) || /\b(pdf|download|fulltext)\b/i.test(cls)) {
                            if (!results.includes(absHref)) results.push(absHref);
                            return;
                        }
                        if (a.dataset && (a.dataset.url || a.dataset.href || a.dataset.resource || a.dataset.pdfUrl)) {
                            const candidate = a.dataset.url || a.dataset.href || a.dataset.resource || a.dataset.pdfUrl;
                            try { const cabs = new URL(candidate, location.href).toString(); if (!results.includes(cabs)) results.push(cabs); } catch(e) {}
                        }
                        // Check resolver services
                        for (const pattern of resolverPatterns) {
                            if (pattern.test(absHref.toLowerCase())) {
                                if (!results.includes(absHref)) results.push(absHref);
                                return;
                            }
                        }
                    } catch (e) {}
                });

                try {
                    Array.from(document.querySelectorAll('iframe[src], embed[src], object[data]')).forEach(el => {
                        try {
                            const src = el.getAttribute('src') || el.getAttribute('data') || '';
                            if (!src) return;
                            if (/\.pdf([?#]|$)/i.test(src) || /(application|pdf)/i.test(el.type || '')) {
                                const abs = new URL(src, location.href).toString(); if (!results.includes(abs)) results.push(abs);
                            }
                        } catch(e) {}
                    });
                } catch (e) {}

                const absResults = results.map(u => {
                    try { return new URL(u, location.href).toString(); } catch (e) { return null; }
                }).filter(Boolean);
                try {
                    window.dispatchEvent(new CustomEvent('bello-response-findAttachments', { detail: { success: true, attachments: absResults } }));
                } catch (e) {
                    // fallback to string-only detail
                    try { window.dispatchEvent(new CustomEvent('bello-response-findAttachments', { detail: { success: true, json: JSON.stringify({ attachments: absResults }) } })); } catch(e){}
                }
            } catch (err) {
                try { window.dispatchEvent(new CustomEvent('bello-response-findAttachments', { detail: { success: false, error: err && err.message ? err.message : String(err) } })); } catch(e){}
            }
        }, false);

        // Resolve an attachment URL: fetch it in page/content context, return either base64 (if PDF)
        // or a list of candidate PDF URLs found in the returned HTML.
        // Enhanced to handle resolver services (LibKey, Unpaywall, etc.) and follow redirects.
        chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
            if (message && message.action === 'resolveAttachment' && message.url) {
                (async () => {
                    try {
                        const u = message.url;
                        console.debug('Bello: resolveAttachment starting for:', u);
                        
                        // Check if this is a known resolver service - these often need special handling
                        const isResolver = /libkey\.|unpaywall\.|oadoi\.|doaj\.|core\.ac\.uk|europepmc\.org|ncbi\.nlm\.nih\.gov\/pmc|arxiv\.org|ssrn\.com|researchgate\.net|academia\.edu|semanticscholar\.org|openaccess|resolve|sfx|linkresolver/i.test(u);
                        
                        const resp = await fetch(u, { credentials: 'include', redirect: 'follow' });
                        const finalUrl = resp.url; // URL after all redirects
                        console.debug('Bello: resolveAttachment fetched, final URL:', finalUrl, 'status:', resp.status);
                        
                        const ct = (resp.headers.get('content-type') || '').toLowerCase();
                        
                        // Check if we got a PDF (either by content-type or by URL pattern)
                        if (ct.includes('application/pdf') || ct.includes('application/octet-stream') || 
                            /\.pdf([?#]|$)/i.test(finalUrl) || /\.pdf([?#]|$)/i.test(u)) {
                            const buf = await resp.arrayBuffer();
                            // Verify it's actually a PDF by checking magic bytes
                            const bytes = new Uint8Array(buf);
                            const isPdfMagic = bytes.length > 4 && bytes[0] === 0x25 && bytes[1] === 0x50 && bytes[2] === 0x44 && bytes[3] === 0x46; // %PDF
                            
                            if (isPdfMagic || ct.includes('application/pdf')) {
                                let binary = '';
                                const chunkSize = 0x8000;
                                for (let i = 0; i < bytes.length; i += chunkSize) {
                                    binary += String.fromCharCode.apply(null, Array.from(bytes.subarray(i, i + chunkSize)));
                                }
                                const b64 = btoa(binary);
                                const cd = resp.headers.get('content-disposition') || '';
                                const m = cd ? (cd.match(/filename\*=UTF-8''([^;\n]+)/i) || cd.match(/filename=\"?([^\";\n]+)\"?/)) : null;
                                let filename = m && m[1] ? decodeURIComponent(m[1]) : (new URL(finalUrl)).pathname.split('/').pop() || 'attachment.pdf';
                                if (!filename.toLowerCase().endsWith('.pdf')) filename += '.pdf';
                                console.debug('Bello: resolveAttachment got PDF, size:', bytes.length, 'filename:', filename);
                                sendResponse({ success: true, pdf: true, filename, mime: 'application/pdf', data: b64 });
                                return;
                            }
                        }

                        // Otherwise parse HTML and search for candidate PDF links
                        const text = await resp.text();
                        const parser = new DOMParser();
                        const doc = parser.parseFromString(text, 'text/html');
                        const found = new Set();
                        
                        // For resolver pages, look for the actual PDF/download links
                        // LibKey and similar services often have a "Download PDF" or "View PDF" button
                        const downloadKeywords = /\b(download|view|get|access|open)\s*(pdf|full\s*text|article|paper)\b/i;
                        
                        // look for anchors, iframes, embeds, links
                        doc.querySelectorAll('a[href], iframe[src], embed[src], object[data], link[href]').forEach(el => {
                            try {
                                const href = el.getAttribute('href') || el.getAttribute('src') || el.getAttribute('data') || '';
                                if (!href || href === '#' || href.startsWith('javascript:')) return;
                                
                                const elText = (el.textContent || '').trim();
                                const elClass = (el.className || '').toLowerCase();
                                const elTitle = (el.getAttribute('title') || '').toLowerCase();
                                
                                // Direct PDF link
                                if (/\.pdf([?#]|$)/i.test(href)) {
                                    try { found.add(new URL(href, finalUrl).toString()); } catch(e) { found.add(href); }
                                    return;
                                }
                                
                                // Links with fulltext/download keywords in URL
                                if (/full-text-file|fulltext|fulltxt|getpdf|download.*pdf|pdf.*download/i.test(href)) {
                                    try { found.add(new URL(href, finalUrl).toString()); } catch(e) { found.add(href); }
                                    return;
                                }
                                
                                // Links with download/view PDF text
                                if (downloadKeywords.test(elText) || downloadKeywords.test(elTitle) || 
                                    /pdf|download|fulltext/i.test(elClass)) {
                                    try { found.add(new URL(href, finalUrl).toString()); } catch(e) { found.add(href); }
                                    return;
                                }
                                
                                // LibKey specific: look for links to publisher sites
                                if (isResolver && /doi\.org|wiley|elsevier|springer|tandfonline|sagepub|oup\.com|cambridge\.org|nature\.com|science\.org/i.test(href)) {
                                    try { found.add(new URL(href, finalUrl).toString()); } catch(e) { found.add(href); }
                                }
                            } catch (e) {}
                        });
                        
                        // also search for obvious URLs in scripts/json (often resolver services embed PDF URLs in JS)
                        try {
                            const scripts = Array.from(doc.querySelectorAll('script')).map(s => s.textContent || '').join('\n');
                            // PDF URLs
                            const urlRx = /https?:\/\/[\w\-./?=&%:#]+?\.pdf(\b|[?#])/ig;
                            let m;
                            while ((m = urlRx.exec(scripts)) !== null) {
                                found.add(m[0]);
                            }
                            // Also look for pdfUrl, downloadUrl, etc in JSON
                            const jsonUrls = scripts.match(/"(pdf[Uu]rl|download[Uu]rl|fulltext[Uu]rl|content[Uu]rl)"\s*:\s*"([^"]+)"/g);
                            if (jsonUrls) {
                                for (const match of jsonUrls) {
                                    const urlMatch = match.match(/"([^"]+)"$/);
                                    if (urlMatch && urlMatch[1]) {
                                        try { found.add(new URL(urlMatch[1], finalUrl).toString()); } catch(e) { found.add(urlMatch[1]); }
                                    }
                                }
                            }
                        } catch (e) {}
                        
                        // Check meta tags for PDF URLs
                        try {
                            doc.querySelectorAll('meta[name*="pdf"], meta[property*="pdf"], meta[content*=".pdf"]').forEach(el => {
                                const content = el.getAttribute('content') || '';
                                if (content && /\.pdf([?#]|$)/i.test(content)) {
                                    try { found.add(new URL(content, finalUrl).toString()); } catch(e) { found.add(content); }
                                }
                            });
                        } catch (e) {}

                        const final = Array.from(found);
                        console.debug('Bello: resolveAttachment found', final.length, 'candidate URLs:', final);
                        sendResponse({ success: true, pdf: false, urls: final, htmlLength: text.length, finalUrl: finalUrl });
                    } catch (err) {
                        console.debug('Bello: resolveAttachment error:', err);
                        sendResponse({ success: false, error: err && err.message ? err.message : String(err) });
                    }
                })();
                return true; // keep channel open for async sendResponse
            }
        });
        window.addEventListener('bello-request-translators', function(_ev) {
            try {
                translator.detectTranslators();
                const list = translator.translators.map(t => ({ id: t.id, name: t.name, priority: t.priority }));
                window.dispatchEvent(new CustomEvent('bello-response-translators', { detail: { success: true, translators: list } }));
            } catch (err) {
                window.dispatchEvent(new CustomEvent('bello-response-translators', { detail: { success: false, error: err && err.message ? err.message : String(err) } }));
            }
        }, false);
    } catch (e) {
        // ignore if page prevents setting window properties
    }
    // Inject a tiny page-context bridge so page scripts / console can call
    // `window.belloConnector.extractWebPage()` directly even when the
    // extension runs in an isolated world. The bridge dispatches the
    // existing `bello-request-extract` CustomEvent and resolves with the
    // `bello-response-extract` detail when available.
    try {
        const injectedCode = `(() => {
            try {
                if (window.belloConnector && typeof window.belloConnector.extractWebPage === 'function') return;
                window.belloConnector = window.belloConnector || {};
                // store last extraction result so console callers can get immediate answers
                window.belloConnector._lastExtract = window.belloConnector._lastExtract || null;
                window.belloConnector.lastExtract = window.belloConnector._lastExtract;

                // synchronous accessor for last cached extraction in the page context
                window.belloConnector.getLastExtract = function() { return window.belloConnector.lastExtract || window.belloConnector._lastExtract || null; };
                // update cache when a response arrives (also mirror to friendly name)
                window.addEventListener('bello-response-extract', function(ev) {
                    try {
                        if (ev && ev.detail) {
                            if (ev.detail.data) {
                                window.belloConnector._lastExtract = ev.detail.data;
                            } else if (ev.detail.json) {
                                try { window.belloConnector._lastExtract = JSON.parse(ev.detail.json); } catch (e) { window.belloConnector._lastExtract = null; }
                            }
                            window.belloConnector.lastExtract = window.belloConnector._lastExtract;
                        }
                    } catch (e) {}
                });

                window.belloConnector.extractWebPage = function() {
                    // always return a Promise; if cached, resolve immediately
                    if (window.belloConnector._lastExtract) return Promise.resolve(window.belloConnector._lastExtract);
                    return new Promise((resolve) => {
                        const onResp = function(ev) {
                            try {
                                if (ev && ev.detail) {
                                    window.removeEventListener('bello-response-extract', onResp);
                                    if (ev.detail.data) {
                                        // mirror to cache/friendly alias
                                        window.belloConnector._lastExtract = ev.detail.data;
                                        window.belloConnector.lastExtract = window.belloConnector._lastExtract;
                                        return resolve(ev.detail.data);
                                    }
                                    if (ev.detail.json) {
                                        try {
                                            const parsed = JSON.parse(ev.detail.json);
                                            window.belloConnector._lastExtract = parsed;
                                            window.belloConnector.lastExtract = parsed;
                                            return resolve(parsed);
                                        } catch (e) { return resolve(undefined); }
                                    }
                                }
                            } catch (e) {}
                        };
                        window.addEventListener('bello-response-extract', onResp);
                        // ask the content script to extract
                        window.dispatchEvent(new CustomEvent('bello-request-extract'));
                        // fallback: if no response within 5000ms, resolve undefined
                        setTimeout(() => { try { window.removeEventListener('bello-response-extract', onResp); } catch(e){}; resolve(undefined); }, 5000);
                    });
                };
            } catch (e) {}
        })();`;

        const s = document.createElement('script');
        s.setAttribute('type', 'text/javascript');
        s.textContent = injectedCode;
        (document.documentElement || document.head || document.body || document).appendChild(s);
        // remove the element after execution to keep DOM clean
        s.parentNode && s.parentNode.removeChild(s);
    } catch (e) {
        // ignore injection failures
    }
    console.debug('Bello content-script: debug API exposed (window.belloConnector)');
    
    // Listen for save requests
    chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
        // Ignore extract requests from inside iframes (e.g. reCAPTCHA) so the top frame
        // is the one that responds with the page metadata. This prevents capturing
        // titles like "reCAPTCHA" from embedded frames.
        if (message.action === 'extractData' && window.top !== window.self) {
            console.debug('Bello content-script: ignoring extractData in iframe');
            return; // no response; background will attempt fallback or other frames
        }

        if (message.action === 'extractData') {
            // Try immediate extraction; if translator not ready (page dynamic), retry a few times
            let attempts = 0;
            const tryExtract = () => {
                attempts += 1;
                try {
                    // refresh detector in case the page changed
                    translator.detectTranslators();
                    const bestTranslator = translator.getBestTranslator();
                    if (bestTranslator) {
                        try {
                            const maybe = bestTranslator.extract();
                            if (maybe && typeof maybe.then === 'function') {
                                maybe.then(data => sendResponse({ success: true, data, translator: bestTranslator.name })).catch(err => sendResponse({ success: false, error: err && err.message ? err.message : String(err) }));
                                return;
                            } else {
                                sendResponse({ success: true, data: maybe, translator: bestTranslator.name });
                                return;
                            }
                        } catch (e) {
                            sendResponse({ success: false, error: e && e.message ? e.message : String(e) });
                            return;
                        }
                    }
                } catch (e) {
                    // fall through to retry logic
                    console.debug('Bello content-script: extract attempt failed', e);
                }
                if (attempts < 4) {
                    // wait a bit and try again (page might inject metadata)
                    setTimeout(tryExtract, 250);
                } else {
                    sendResponse({ success: false, error: 'No suitable translator found after retries' });
                }
            };
            tryExtract();
            return true; // keep message channel open for async response
        }
        
        if (message.action === 'getTranslators') {
            sendResponse({ 
                success: true, 
                translators: translator.translators.map(t => ({
                    id: t.id,
                    name: t.name,
                    priority: t.priority
                }))
            });
            return true;
        }
        // Find candidate attachments (PDFs) on the page and return URLs
        if (message.action === 'findAttachments') {
            try {
                const results = [];
                // meta tags commonly used by publishers
                const metaSelectors = [
                    'meta[name="citation_pdf_url"]',
                    'meta[name="citation_fulltext_pdf"]',
                    'meta[name="citation_fulltext_html_url"]',
                    'meta[name="pdf_url"]',
                    'link[type="application/pdf"]',
                    'meta[property="og:pdf"]'
                ];
                for (const sel of metaSelectors) {
                    document.querySelectorAll(sel).forEach(el => {
                        const v = el.getAttribute('content') || el.getAttribute('href') || el.getAttribute('src') || '';
                        if (v && !results.includes(v)) results.push(v);
                    });
                }

                // ======= Primo / Ex Libris specific detection =======
                // Primo pages often have "View Online", "Full Text Available", "Online Access" buttons
                // These are typically inside prm-full-view-service-container or similar Angular components
                const isPrimoPage = /primo|exlibris|discovery\/fulldisplay/i.test(location.href) || 
                                    document.querySelector('prm-full-view, prm-full-view-service-container, [class*="primo"]') !== null;
                
                if (isPrimoPage) {
                    console.debug('Bello: Primo/ExLibris page detected, applying specialized extraction');
                    
                    // Look for Primo service containers with links
                    const primoServiceSelectors = [
                        'prm-full-view-service-container a[href]',
                        'prm-service-button a[href]',
                        'prm-view-online a[href]',
                        'prm-alma-viewit a[href]',
                        'prm-alma-openurl a[href]',
                        '.service-type a[href]',
                        '.getit-link a[href]',
                        '.full-view-inner-container a[href]',
                        '[class*="viewit"] a[href]',
                        '[class*="openurl"] a[href]',
                        '[class*="full-text"] a[href]',
                        '[class*="fulltext"] a[href]'
                    ];
                    
                    for (const sel of primoServiceSelectors) {
                        try {
                            document.querySelectorAll(sel).forEach(a => {
                                const href = (a.getAttribute('href') || '').trim();
                                if (!href || href === '#') return;
                                const absHref = (() => { try { return new URL(href, location.href).toString(); } catch(e) { return href; } })();
                                if (!results.includes(absHref)) {
                                    console.debug('Bello: Found Primo service link:', absHref);
                                    results.push(absHref);
                                }
                            });
                        } catch (e) {}
                    }
                    
                    // Look for buttons/links with Open Access, View Online, Full Text text
                    const oaKeywords = /\b(open access|view online|online access|full text|fulltext|view pdf|download pdf|get pdf|free access|free pdf)\b/i;
                    document.querySelectorAll('a[href], button[onclick], [role="button"]').forEach(el => {
                        try {
                            const text = (el.textContent || '').trim();
                            const ariaLabel = el.getAttribute('aria-label') || '';
                            const title = el.getAttribute('title') || '';
                            
                            if (oaKeywords.test(text) || oaKeywords.test(ariaLabel) || oaKeywords.test(title)) {
                                // Try to get href directly
                                let href = el.getAttribute('href') || '';
                                
                                // For buttons, look for onclick or data attributes
                                if (!href || href === '#') {
                                    href = el.getAttribute('data-url') || el.getAttribute('data-href') || 
                                           el.getAttribute('data-link') || el.getAttribute('ng-href') || '';
                                }
                                
                                // Check parent anchor if this is a span/icon inside a link
                                if (!href || href === '#') {
                                    const parentA = el.closest('a[href]');
                                    if (parentA) href = parentA.getAttribute('href') || '';
                                }
                                
                                if (href && href !== '#') {
                                    const absHref = (() => { try { return new URL(href, location.href).toString(); } catch(e) { return href; } })();
                                    if (!results.includes(absHref)) {
                                        console.debug('Bello: Found OA/FullText link:', absHref, 'from text:', text.slice(0, 50));
                                        results.push(absHref);
                                    }
                                }
                            }
                        } catch (e) {}
                    });
                    
                    // Primo sometimes embeds links in Angular ng-click or data attributes
                    document.querySelectorAll('[ng-click], [data-url], [data-pdfurl], [data-fulltext-url]').forEach(el => {
                        try {
                            const url = el.getAttribute('data-url') || el.getAttribute('data-pdfurl') || 
                                       el.getAttribute('data-fulltext-url') || '';
                            if (url && !results.includes(url)) {
                                const absUrl = (() => { try { return new URL(url, location.href).toString(); } catch(e) { return url; } })();
                                if (!results.includes(absUrl)) results.push(absUrl);
                            }
                        } catch (e) {}
                    });
                }

                // ======= Resolver/OA service detection =======
                // Common resolver and OA services that redirect to PDFs
                const resolverPatterns = [
                    /libkey\.(io|org|link)/i,
                    /unpaywall\.org/i,
                    /oadoi\.org/i,
                    /doaj\.org/i,
                    /core\.ac\.uk/i,
                    /europepmc\.org/i,
                    /ncbi\.nlm\.nih\.gov\/pmc/i,
                    /arxiv\.org/i,
                    /ssrn\.com/i,
                    /researchgate\.net/i,
                    /academia\.edu/i,
                    /semanticscholar\.org/i,
                    /openaccess/i,
                    /fulltxt/i,
                    /full-text/i,
                    /getft/i,
                    /resolve/i,
                    /sfx/i,
                    /linkresolver/i
                ];

                // Look for obvious anchors linking to PDFs and anchors whose text or classes indicate PDF/fulltext/download
                document.querySelectorAll('a[href]').forEach(a => {
                    try {
                        const href = (a.getAttribute('href')||'').trim();
                        if (!href || href === '#' || href.startsWith('javascript:')) return;
                        const absHref = (() => { try { return new URL(href, location.href).toString(); } catch(e) { return href; } })();
                        const lower = href.split('?')[0].toLowerCase();
                        const text = (a.textContent||'').trim().toLowerCase();
                        const cls = (a.className||'').toLowerCase();
                        const ariaLabel = (a.getAttribute('aria-label') || '').toLowerCase();
                        
                        // direct PDF link
                        if (lower.endsWith('.pdf') || /\.pdf([?#]|$)/i.test(href)) {
                            if (!results.includes(absHref)) results.push(absHref);
                            return;
                        }
                        // anchors that mention 'pdf', 'full text', 'download', 'view online'
                        if (/\b(pdf|full text|download|fulltext|view online|view pdf|open pdf|online access|get it|access article)\b/i.test(text) || 
                            /\b(pdf|download|fulltext|viewonline|openaccess)\b/i.test(cls) ||
                            /\b(pdf|full text|download|view online)\b/i.test(ariaLabel)) {
                            if (!results.includes(absHref)) results.push(absHref);
                            return;
                        }
                        // some Primo links use data-attributes pointing to resources
                        if (a.dataset && (a.dataset.url || a.dataset.href || a.dataset.resource || a.dataset.pdfUrl)) {
                            const candidate = a.dataset.url || a.dataset.href || a.dataset.resource || a.dataset.pdfUrl;
                            try { const cabs = new URL(candidate, location.href).toString(); if (!results.includes(cabs)) results.push(cabs); } catch(e) {}
                        }
                        // Check for resolver services
                        try {
                            const lh = absHref.toLowerCase();
                            for (const pattern of resolverPatterns) {
                                if (pattern.test(lh)) {
                                    if (!results.includes(absHref)) {
                                        console.debug('Bello: Found resolver/OA link:', absHref);
                                        results.push(absHref);
                                    }
                                    return;
                                }
                            }
                        } catch (e) {}
                    } catch (e) {}
                });

                // ======= Unpaywall widget detection =======
                // Unpaywall browser extension adds a widget; look for its data
                try {
                    const unpaywall = document.querySelector('[data-unpaywall-url], .unpaywall-icon, #unpaywall');
                    if (unpaywall) {
                        const url = unpaywall.getAttribute('data-unpaywall-url') || unpaywall.getAttribute('href');
                        if (url && !results.includes(url)) {
                            console.debug('Bello: Found Unpaywall link:', url);
                            results.push(url);
                        }
                    }
                } catch (e) {}

                // JSON-LD or structured data: look for contentUrl / encoding
                try {
                    const jlds = Array.from(document.querySelectorAll('script[type="application/ld+json"]')).map(s => s.textContent).filter(Boolean);
                    for (const raw of jlds) {
                        try {
                            const obj = JSON.parse(raw);
                            const searchObj = (o) => {
                                if (!o || typeof o !== 'object') return;
                                if (Array.isArray(o)) { o.forEach(searchObj); return; }
                                if (o.contentUrl && typeof o.contentUrl === 'string') {
                                    const u = new URL(o.contentUrl, location.href).toString(); if (!results.includes(u)) results.push(u);
                                }
                                if (o.encoding && typeof o.encoding === 'object') {
                                    if (o.encoding.contentUrl && typeof o.encoding.contentUrl === 'string') { const u = new URL(o.encoding.contentUrl, location.href).toString(); if (!results.includes(u)) results.push(u); }
                                }
                                // Also check for url field which might contain PDF link
                                if (o.url && typeof o.url === 'string' && /\.pdf([?#]|$)/i.test(o.url)) {
                                    const u = new URL(o.url, location.href).toString(); if (!results.includes(u)) results.push(u);
                                }
                                for (const k of Object.keys(o)) searchObj(o[k]);
                            };
                            searchObj(obj);
                        } catch (e) {}
                    }
                } catch (e) {}

                // Also look for embedded PDFs (iframe/embed/object) and elements with PDF-like src
                try {
                    Array.from(document.querySelectorAll('iframe[src], embed[src], object[data]')).forEach(el => {
                        try {
                            const src = el.getAttribute('src') || el.getAttribute('data') || '';
                            if (!src) return;
                            if (/\.pdf([?#]|$)/i.test(src) || /(application|pdf)/i.test(el.type || '')) {
                                const abs = new URL(src, location.href).toString(); if (!results.includes(abs)) results.push(abs);
                            }
                        } catch(e) {}
                    });
                } catch (e) {}

                // Return array of unique absolute URLs
                const absResults = results.map(u => {
                    try { return new URL(u, location.href).toString(); } catch (e) { return null; }
                }).filter(Boolean);
                console.debug('Bello: findAttachments found', absResults.length, 'candidate URLs:', absResults);
                sendResponse({ success: true, attachments: absResults });
            } catch (err) {
                sendResponse({ success: false, error: err && err.message ? err.message : String(err) });
            }
            return true;
        }
        
        // Handle getPdfContent - for when we're on a PDF page and need to extract the content
        if (message.action === 'getPdfContent') {
            try {
                // Check if we're on a PDF page
                const isPdfPage = document.contentType === 'application/pdf' || 
                                  /\.pdf([?#]|$)/i.test(location.href) ||
                                  document.querySelector('embed[type="application/pdf"]') !== null;
                
                if (isPdfPage) {
                    // Try to get PDF via fetch from current URL
                    fetch(location.href, { credentials: 'include' })
                        .then(response => {
                            if (response.ok && (response.headers.get('content-type') || '').includes('application/pdf')) {
                                return response.arrayBuffer();
                            }
                            throw new Error('Not a PDF');
                        })
                        .then(buffer => {
                            // Convert to base64
                            const bytes = new Uint8Array(buffer);
                            let binary = '';
                            for (let i = 0; i < bytes.byteLength; i++) {
                                binary += String.fromCharCode(bytes[i]);
                            }
                            const base64 = btoa(binary);
                            
                            // Extract filename from URL
                            let filename = 'attachment.pdf';
                            try {
                                const path = new URL(location.href).pathname;
                                const parts = path.split('/');
                                const lastPart = parts[parts.length - 1];
                                if (lastPart && lastPart.toLowerCase().includes('.pdf')) {
                                    filename = decodeURIComponent(lastPart);
                                }
                            } catch(e) {}
                            
                            sendResponse({ success: true, data: base64, filename: filename });
                        })
                        .catch(err => {
                            sendResponse({ success: false, error: err.message });
                        });
                    return true; // async response
                } else {
                    sendResponse({ success: false, error: 'Not a PDF page' });
                }
            } catch (err) {
                sendResponse({ success: false, error: err && err.message ? err.message : String(err) });
            }
            return true;
        }
    });
    
    // Page indicator removed: the extension now uses the browser action (extension button)
    // The content script still exposes `extractData` via the message listener above.
    
})();