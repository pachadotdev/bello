/*
 * Bello Connector - Background Script
 * Saves bibliographic entries to Bello with optional PDF attachments
 */

// Configuration
const BELLO_CONFIG = {
    name: 'Bello',
    version: '1.0.0',
    localPort: 1842
};

// State to prevent double-clicks
let isSaving = false;
let currentSaveTabId = null;

// ============================================================================
// MESSAGE HANDLERS
// ============================================================================

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    switch(message.action) {
        case 'saveItem':
            handleSaveItem(message.data, sender.tab)
                .then(sendResponse)
                .catch(err => sendResponse({error: err.message}));
            return true;
            
        case 'detectTranslators':
            detectPageTranslators(sender.tab)
                .then(sendResponse)
                .catch(err => sendResponse({error: err.message}));
            return true;
            
        default:
            sendResponse({error: 'Unknown action'});
    }
});

// Handle browser action click
chrome.browserAction.onClicked.addListener((tab) => {
    // Prevent double-clicks
    if (isSaving && currentSaveTabId === tab.id) {
        console.debug('Bello: save already in progress, ignoring click');
        return;
    }
    handleQuickSave(tab);
});

// ============================================================================
// BADGE HELPERS (the "comic balloon" feedback)
// ============================================================================

function setBadge(tabId, text, color) {
    try {
        chrome.browserAction.setBadgeText({ text: text, tabId: tabId });
        chrome.browserAction.setBadgeBackgroundColor({ color: color, tabId: tabId });
    } catch (e) {
        console.debug('Bello: setBadge error', e);
    }
}

function clearBadge(tabId) {
    try {
        chrome.browserAction.setBadgeText({ text: '', tabId: tabId });
    } catch (e) {}
}

function showSavingBadge(tabId) {
    setBadge(tabId, '...', '#2196F3'); // Blue = working
}

function showSuccessBadge(tabId) {
    setBadge(tabId, '✓', '#4CAF50'); // Green = success
    setTimeout(() => clearBadge(tabId), 3000);
}

function showWarningBadge(tabId, message) {
    setBadge(tabId, '!', '#FF9800'); // Orange = warning (saved but no PDF)
    showNotification(message, 'warning');
    setTimeout(() => clearBadge(tabId), 5000);
}

function showErrorBadge(tabId, message) {
    setBadge(tabId, '✗', '#F44336'); // Red = error
    showNotification(message, 'error');
    setTimeout(() => clearBadge(tabId), 5000);
}

// ============================================================================
// MAIN SAVE FLOW
// 1. Show badge immediately ("...")
// 2. Extract metadata and save entry right away
// 3. Try to download PDF in background (silently)
// 4. Update badge: "✓" with PDF, "!" without PDF, "✗" on error
// ============================================================================

async function handleQuickSave(tab) {
    isSaving = true;
    currentSaveTabId = tab.id;
    showSavingBadge(tab.id);
    
    try {
        console.debug('Bello: quick save triggered', tab && tab.url);
        
        // Extract metadata from the page
        const response = await new Promise((resolve) => {
            chrome.tabs.sendMessage(tab.id, { action: 'extractData' }, { frameId: 0 }, (resp) => {
                resolve(resp);
            });
        });
        
        let itemData = null;
        
        if (!response || !response.success) {
            console.warn('Bello: main extractor failed, trying fallback');
            itemData = await extractFallback(tab);
        } else {
            itemData = response.data;
        }
        
        if (!itemData || !itemData.title) {
            showErrorBadge(tab.id, 'Could not extract page metadata');
            return;
        }
        
        // STEP 1: Save entry immediately (without attachments)
        console.debug('Bello: saving entry:', itemData.title);
        let savedItemId = null;
        try {
            const saveResult = await sendToBello(itemData);
            savedItemId = saveResult && saveResult.id ? saveResult.id : null;
            console.debug('Bello: entry saved, id:', savedItemId);
        } catch (err) {
            console.error('Bello: failed to save entry', err);
            showErrorBadge(tab.id, 'Failed to save: ' + err.message);
            return;
        }
        
        // STEP 2: Try to get PDF from Unpaywall (if we have a DOI)
        console.debug('Bello: looking for PDFs via Unpaywall...');
        let pdfAttached = false;
        
        try {
            const doi = itemData.doi || null;
            if (doi) {
                const unpaywallPdf = await tryUnpaywallFallback(tab, doi);
                if (unpaywallPdf) {
                    console.debug('Bello: got PDF from Unpaywall!');
                    console.debug('Bello: PDF filename:', unpaywallPdf.filename);
                    console.debug('Bello: PDF data length:', unpaywallPdf.data ? unpaywallPdf.data.length : 0);
                    // Update the saved entry with the PDF attachment
                    // Include DOI so the C++ code can find the existing item
                    const updateData = {
                        id: savedItemId,
                        doi: doi,
                        attachments: [unpaywallPdf]
                    };
                    console.debug('Bello: sending attachment update:', JSON.stringify({
                        id: updateData.id,
                        doi: updateData.doi,
                        attachmentsCount: updateData.attachments.length,
                        firstAttachmentFilename: updateData.attachments[0] && updateData.attachments[0].filename
                    }));
                    const updateResult = await sendToBello(updateData);
                    console.debug('Bello: attachment update result:', JSON.stringify(updateResult));
                    if (updateResult && updateResult.success) {
                        pdfAttached = true;
                    } else {
                        console.warn('Bello: attachment update failed!', updateResult);
                    }
                }
            } else {
                console.debug('Bello: no DOI available for Unpaywall lookup');
            }
        } catch (attachErr) {
            console.debug('Bello: PDF attachment failed', attachErr);
        }
        
        // STEP 3: Show final status
        if (pdfAttached) {
            showSuccessBadge(tab.id);
            showNotification('Saved to Bello with PDF!');
        } else {
            showWarningBadge(tab.id, 'Saved to Bello (no PDF found - you may need to log in)');
        }
        
    } catch (error) {
        console.error('Quick save failed:', error);
        showErrorBadge(tab.id, 'Failed: ' + error.message);
    } finally {
        isSaving = false;
        currentSaveTabId = null;
    }
}

// Fallback metadata extraction when content script fails
async function extractFallback(tab) {
    return new Promise((resolve) => {
        chrome.tabs.executeScript(tab.id, {
            code: `(() => {
                try {
                    const getMeta = (sel) => { 
                        const el = document.querySelector(sel); 
                        return el ? (el.getAttribute('content')||el.textContent) : ''; 
                    };
                    const findDOI = () => {
                        const doiMeta = document.querySelector('meta[name="citation_doi"], meta[name="DOI"]');
                        if (doiMeta) return (doiMeta.getAttribute('content')||'').replace(/^(doi:|https?:\\/\\/(dx\\.)?doi\\.org\\/)/i,'');
                        const m = document.body.textContent.match(/10\\.\\d{4,9}\\/[^\\s\\)\\]\\"']+/);
                        return m ? m[0] : '';
                    };
                    const extractAuthors = () => {
                        const nodes = Array.from(document.querySelectorAll('meta[name="citation_author"]'))
                            .map(n=>n.getAttribute('content')||n.textContent).filter(Boolean);
                        if (nodes.length) return nodes.join(' and ');
                        const a = document.querySelector('meta[name="author"]');
                        return a ? (a.getAttribute('content')||a.textContent) : '';
                    };
                    return {
                        itemType: 'webpage',
                        title: (document.title||'').trim(),
                        url: location.href,
                        authors: extractAuthors(),
                        year: '',
                        doi: findDOI(),
                        isbn: '',
                        abstract: getMeta('meta[name="description"]')
                    };
                } catch (e) { return null; }
            })();`
        }, (results) => {
            if (chrome.runtime.lastError) {
                console.error('Bello: fallback extraction failed', chrome.runtime.lastError);
                resolve(null);
            } else {
                resolve(results && results[0] ? results[0] : null);
            }
        });
    });
}

// ============================================================================
// PDF ATTACHMENT - Silent (no visible tabs)
// ============================================================================

function encodePdfToBase64(buf) {
    const bytes = new Uint8Array(buf);
    let binary = '';
    const chunkSize = 0x8000;
    for (let i = 0; i < bytes.length; i += chunkSize) {
        binary += String.fromCharCode.apply(null, Array.from(bytes.subarray(i, i + chunkSize)));
    }
    return btoa(binary);
}

function isPdfBuffer(buf) {
    const bytes = new Uint8Array(buf);
    return bytes.length > 4 && bytes[0] === 0x25 && bytes[1] === 0x50 && bytes[2] === 0x44 && bytes[3] === 0x46;
}

function getFilename(response, url) {
    const cd = response.headers.get('content-disposition') || '';
    const m = cd ? (cd.match(/filename\*=UTF-8''([^;\n]+)/i) || cd.match(/filename="?([^";\n]+)"?/)) : null;
    if (m && m[1]) return decodeURIComponent(m[1]);
    try { 
        let fname = (new URL(url)).pathname.split('/').pop() || 'attachment.pdf';
        if (!fname.toLowerCase().endsWith('.pdf')) fname += '.pdf';
        return fname;
    } catch(e) { return 'attachment.pdf'; }
}

// Check if URL is a resolver service that needs special handling
function isResolverUrl(url) {
    const resolverPatterns = [
        /libkey\.io/i,
        /unpaywall\.org/i,
        /oadoi\.org/i,
        /doi\.org/i,
        /getfulltxt/i,
        /ezproxy/i,
        /openurl/i,
        /resolver/i,
        /link\.springer/i,
        /sciencedirect/i,
        /wiley\.com/i,
        /tandfonline/i,
        /jstor\.org/i,
        /sagepub/i,
        /cambridge\.org/i,
        /oxford.*journals/i,
        /silverchair/i
    ];
    return resolverPatterns.some(p => p.test(url));
}

// ============================================================================
// PDF ATTACHMENT MONITOR
// Inspired by Zotero's BrowserAttachmentMonitor
// Uses webRequest to intercept PDF responses and capture the URL
// ============================================================================

const PdfAttachmentMonitor = {
    _listeners: new Map(),
    
    // Wait for a PDF to be loaded in a specific tab
    // Returns the PDF URL when detected via Content-Type header
    waitForPdf: function(tabId, timeout = 30000) {
        return new Promise((resolve) => {
            let resolved = false;
            let timeoutId = null;
            
            const done = (result) => {
                if (resolved) return;
                resolved = true;
                if (timeoutId) clearTimeout(timeoutId);
                this._removeListener(tabId);
                resolve(result);
            };
            
            timeoutId = setTimeout(() => done(null), timeout);
            
            // Create listener for this tab
            const listener = (details) => {
                // Only care about main_frame or sub_frame in our target tab
                if (details.tabId !== tabId) return;
                if (details.type !== 'main_frame' && details.type !== 'sub_frame') return;
                
                // Check response headers for PDF content type
                const contentType = details.responseHeaders?.find(
                    h => h.name.toLowerCase() === 'content-type'
                )?.value || '';
                
                const contentDisposition = details.responseHeaders?.find(
                    h => h.name.toLowerCase() === 'content-disposition'
                )?.value || '';
                
                const isPdf = contentType.toLowerCase().includes('application/pdf') ||
                              contentType.toLowerCase().includes('application/octet-stream') ||
                              contentDisposition.toLowerCase().includes('.pdf') ||
                              /\.pdf([?#]|$)/i.test(details.url);
                
                if (isPdf) {
                    console.debug('Bello: PDF detected via webRequest:', details.url);
                    done({ url: details.url, contentType, contentDisposition });
                }
            };
            
            this._addListener(tabId, listener);
        });
    },
    
    _addListener: function(tabId, listener) {
        // Store listener for cleanup
        this._listeners.set(tabId, listener);
        
        // Add to webRequest (Firefox uses browser.webRequest)
        const webRequest = chrome.webRequest || (typeof browser !== 'undefined' && browser.webRequest);
        if (webRequest && webRequest.onHeadersReceived) {
            webRequest.onHeadersReceived.addListener(
                listener,
                { urls: ['<all_urls>'], tabId: tabId, types: ['main_frame', 'sub_frame'] },
                ['responseHeaders']
            );
        }
    },
    
    _removeListener: function(tabId) {
        const listener = this._listeners.get(tabId);
        if (listener) {
            const webRequest = chrome.webRequest || (typeof browser !== 'undefined' && browser.webRequest);
            if (webRequest && webRequest.onHeadersReceived) {
                try {
                    webRequest.onHeadersReceived.removeListener(listener);
                } catch (e) {}
            }
            this._listeners.delete(tabId);
        }
    }
};

// Fetch PDF using a hidden tab with webRequest monitoring
async function fetchPdfViaHiddenTab(url, timeout = 30000) {
    return new Promise(async (resolve) => {
        let tabId = null;
        let timeoutId = null;
        let resolved = false;
        
        const cleanup = () => {
            if (timeoutId) clearTimeout(timeoutId);
            if (tabId) {
                PdfAttachmentMonitor._removeListener(tabId);
                try { chrome.tabs.remove(tabId); } catch(e) {}
            }
        };
        
        const done = (result) => {
            if (resolved) return;
            resolved = true;
            cleanup();
            resolve(result);
        };
        
        timeoutId = setTimeout(() => {
            console.debug('Bello: hidden tab timeout for', url);
            done(null);
        }, timeout);
        
        try {
            // Create a hidden tab
            const tab = await new Promise((res) => {
                chrome.tabs.create({ url: url, active: false }, res);
            });
            tabId = tab.id;
            
            // Start monitoring for PDF response BEFORE the page loads
            const pdfPromise = PdfAttachmentMonitor.waitForPdf(tabId, timeout - 1000);
            
            // Wait for either: PDF detected via headers, or tab finished loading
            const waitForLoad = () => new Promise((res) => {
                const checkTab = (updatedTabId, changeInfo) => {
                    if (updatedTabId === tabId && changeInfo.status === 'complete') {
                        chrome.tabs.onUpdated.removeListener(checkTab);
                        res('loaded');
                    }
                };
                chrome.tabs.onUpdated.addListener(checkTab);
                // Also check immediately in case already loaded
                chrome.tabs.get(tabId, (t) => {
                    if (t && t.status === 'complete') {
                        chrome.tabs.onUpdated.removeListener(checkTab);
                        res('loaded');
                    }
                });
            });
            
            // Race: PDF detected vs page loaded
            const raceResult = await Promise.race([
                pdfPromise.then(r => ({ type: 'pdf', result: r })),
                waitForLoad().then(r => ({ type: 'loaded', result: r }))
            ]);
            
            if (raceResult.type === 'pdf' && raceResult.result) {
                // PDF detected via webRequest headers!
                const pdfUrl = raceResult.result.url;
                console.debug('Bello: fetching detected PDF:', pdfUrl);
                
                try {
                    const resp = await fetch(pdfUrl, { credentials: 'include' });
                    if (resp.ok) {
                        const ct = (resp.headers.get('content-type') || '').toLowerCase();
                        if (ct.includes('application/pdf') || ct.includes('application/octet-stream')) {
                            const buf = await resp.arrayBuffer();
                            if (buf && isPdfBuffer(buf)) {
                                done({ 
                                    url: pdfUrl, 
                                    data: encodePdfToBase64(buf), 
                                    filename: getFilename(resp, pdfUrl) 
                                });
                                return;
                            }
                        }
                    }
                } catch(e) {
                    console.debug('Bello: error fetching detected PDF', e);
                }
            }
            
            // Page loaded but no PDF detected via headers
            // Wait a bit more for any JavaScript-triggered downloads
            await new Promise(r => setTimeout(r, 2000));
            
            // Check if PDF was detected after the page loaded
            const laterPdf = await Promise.race([
                pdfPromise,
                new Promise(r => setTimeout(() => r(null), 3000))
            ]);
            
            if (laterPdf && laterPdf.url) {
                console.debug('Bello: late PDF detected:', laterPdf.url);
                try {
                    const resp = await fetch(laterPdf.url, { credentials: 'include' });
                    if (resp.ok) {
                        const buf = await resp.arrayBuffer();
                        if (buf && isPdfBuffer(buf)) {
                            done({ 
                                url: laterPdf.url, 
                                data: encodePdfToBase64(buf), 
                                filename: getFilename(resp, laterPdf.url) 
                            });
                            return;
                        }
                    }
                } catch(e) {}
            }
            
            // Get the final URL
            const finalTab = await new Promise((res) => chrome.tabs.get(tabId, res));
            const finalUrl = finalTab ? finalTab.url : url;
            
            console.debug('Bello: hidden tab final URL:', finalUrl);
            
            // Check if we landed on a PDF page (URL contains .pdf)
            if (/\.pdf([?#]|$)/i.test(finalUrl)) {
                try {
                    const resp = await fetch(finalUrl, { credentials: 'include' });
                    if (resp.ok) {
                        const ct = (resp.headers.get('content-type') || '').toLowerCase();
                        if (ct.includes('application/pdf') || ct.includes('application/octet-stream')) {
                            const buf = await resp.arrayBuffer();
                            if (buf && isPdfBuffer(buf)) {
                                done({ 
                                    url: finalUrl, 
                                    data: encodePdfToBase64(buf), 
                                    filename: getFilename(resp, finalUrl) 
                                });
                                return;
                            }
                        }
                    }
                } catch(e) {}
            }
            
            // Try to get PDF links from the page via content script
            const links = await new Promise((res) => {
                chrome.tabs.sendMessage(tabId, { action: 'findAttachments' }, { frameId: 0 }, (r) => {
                    res(r || null);
                });
            });
            
            if (links && links.success && links.attachments && links.attachments.length) {
                // Return the URLs found for further processing
                done({ urls: links.attachments });
                return;
            }
            
            done(null);
        } catch (e) {
            console.debug('Bello: hidden tab error', e);
            done(null);
        }
    });
}

// Try to find and download PDFs without opening visible tabs
async function tryAttachPDFsSilently(tab) {
    const MAX_BYTES = 25 * 1024 * 1024;
    const attachments = [];
    
    try {
        // Ask content script to find PDF links
        const resp = await new Promise((resolve) => {
            chrome.tabs.sendMessage(tab.id, { action: 'findAttachments' }, { frameId: 0 }, resolve);
        });
        
        if (!resp || !resp.success || !resp.attachments || !resp.attachments.length) {
            console.debug('Bello: no PDF URLs found on page');
            return attachments;
        }
        
        let urls = resp.attachments;
        console.debug('Bello: found', urls.length, 'potential PDF URLs');
        
        // Prioritize resolver URLs (LibKey, etc.) - they're most likely to have the PDF
        const resolverUrls = urls.filter(u => isResolverUrl(u));
        const otherUrls = urls.filter(u => !isResolverUrl(u));
        urls = [...resolverUrls, ...otherUrls];
        
        // Try resolver URLs first with hidden tab approach
        for (const u of resolverUrls) {
            if (attachments.length > 0) break;
            
            console.debug('Bello: trying resolver via hidden tab:', u);
            const result = await fetchPdfViaHiddenTab(u);
            
            if (result) {
                if (result.data) {
                    attachments.push({
                        filename: result.filename || 'attachment.pdf',
                        mime: 'application/pdf',
                        data: result.data
                    });
                    console.debug('Bello: got PDF from resolver:', result.filename);
                    break;
                } else if (result.urls && result.urls.length) {
                    // Try the URLs found on the resolved page
                    for (const ru of result.urls) {
                        try {
                            const r = await fetch(ru, { credentials: 'include', redirect: 'follow' });
                            if (r.ok) {
                                const ct = (r.headers.get('content-type') || '').toLowerCase();
                                if (ct.includes('application/pdf') || /\.pdf([?#]|$)/i.test(ru)) {
                                    const buf = await r.arrayBuffer();
                                    if (buf && buf.byteLength <= MAX_BYTES && isPdfBuffer(buf)) {
                                        attachments.push({
                                            filename: getFilename(r, ru),
                                            mime: 'application/pdf',
                                            data: encodePdfToBase64(buf)
                                        });
                                        console.debug('Bello: got PDF from resolved URL');
                                        break;
                                    }
                                }
                            }
                        } catch(e) {}
                    }
                }
            }
        }
        
        // Try other URLs with direct fetch
        for (const u of otherUrls) {
            if (attachments.length > 0) break;
            
            try {
                console.debug('Bello: trying direct fetch:', u);
                const fetchResp = await fetch(u, { credentials: 'include', redirect: 'follow' });
                const finalUrl = fetchResp.url;
                
                if (!fetchResp.ok) continue;
                
                const ct = (fetchResp.headers.get('content-type') || '').toLowerCase();
                
                // Is it a PDF?
                if (ct.includes('application/pdf') || ct.includes('application/octet-stream') || 
                    /\.pdf([?#]|$)/i.test(finalUrl) || /\.pdf([?#]|$)/i.test(u)) {
                    
                    const buf = await fetchResp.arrayBuffer();
                    if (buf && buf.byteLength > 0 && buf.byteLength <= MAX_BYTES && 
                        (isPdfBuffer(buf) || ct.includes('application/pdf'))) {
                        attachments.push({ 
                            filename: getFilename(fetchResp, finalUrl), 
                            mime: 'application/pdf', 
                            data: encodePdfToBase64(buf) 
                        });
                        console.debug('Bello: downloaded PDF', attachments[0].filename);
                        break;
                    }
                }
            } catch (e) {
                console.debug('Bello: error fetching', u, e.message || e);
            }
        }
    } catch (e) {
        console.debug('Bello: tryAttachPDFsSilently error', e);
    }
    
    return attachments;
}

// ============================================================================
// UNPAYWALL API FALLBACK
// Free API to get Open Access PDF links for DOIs
// ============================================================================

async function tryUnpaywallFallback(tab, doi) {
    if (!doi) {
        // Try to extract DOI from page
        try {
            const doiResult = await new Promise((resolve) => {
                chrome.tabs.sendMessage(tab.id, { action: 'extractData' }, { frameId: 0 }, resolve);
            });
            doi = doiResult && doiResult.data && doiResult.data.doi;
        } catch (e) {}
    }
    
    if (!doi) {
        console.debug('Bello: no DOI found for Unpaywall lookup');
        return null;
    }
    
    // Clean DOI
    doi = doi.replace(/^(doi:|https?:\/\/(dx\.)?doi\.org\/)/i, '').trim();
    
    console.debug('Bello: trying Unpaywall API for DOI:', doi);
    
    try {
        // Unpaywall API requires a valid email address
        // Using a generic institutional pattern - users can configure their own
        const email = 'mv00622@surrey.ac.uk';
        const apiUrl = `https://api.unpaywall.org/v2/${encodeURIComponent(doi)}?email=${encodeURIComponent(email)}`;
        const resp = await fetch(apiUrl);
        
        if (!resp.ok) {
            console.debug('Bello: Unpaywall API returned', resp.status);
            return null;
        }
        
        const data = await resp.json();
        
        // Look for best OA location with PDF
        let pdfUrl = null;
        
        // Check best_oa_location first
        if (data.best_oa_location && data.best_oa_location.url_for_pdf) {
            pdfUrl = data.best_oa_location.url_for_pdf;
        }
        
        // Fall back to any OA location with PDF
        if (!pdfUrl && data.oa_locations && data.oa_locations.length) {
            for (const loc of data.oa_locations) {
                if (loc.url_for_pdf) {
                    pdfUrl = loc.url_for_pdf;
                    break;
                }
            }
        }
        
        if (pdfUrl) {
            console.debug('Bello: Unpaywall found PDF at:', pdfUrl);
            
            // Try to fetch the PDF
            try {
                console.debug('Bello: attempting to fetch PDF from:', pdfUrl);
                const pdfResp = await fetch(pdfUrl, { redirect: 'follow' });
                console.debug('Bello: fetch response status:', pdfResp.status, pdfResp.ok);
                if (pdfResp.ok) {
                    const ct = (pdfResp.headers.get('content-type') || '').toLowerCase();
                    console.debug('Bello: content-type:', ct);
                    if (ct.includes('application/pdf') || ct.includes('application/octet-stream')) {
                        const buf = await pdfResp.arrayBuffer();
                        console.debug('Bello: buffer size:', buf ? buf.byteLength : 0);
                        if (buf && isPdfBuffer(buf)) {
                            const result = {
                                filename: getFilename(pdfResp, pdfUrl),
                                mime: 'application/pdf',
                                data: encodePdfToBase64(buf)
                            };
                            console.debug('Bello: returning PDF object, filename:', result.filename, 'data length:', result.data.length);
                            return result;
                        } else {
                            console.debug('Bello: buffer failed isPdfBuffer check');
                        }
                    } else {
                        console.debug('Bello: unexpected content-type, not PDF');
                    }
                }
            } catch (e) {
                console.debug('Bello: error fetching Unpaywall PDF', e);
            }
        } else {
            console.debug('Bello: Unpaywall found no OA PDF for this DOI');
        }
    } catch (e) {
        console.debug('Bello: Unpaywall API error', e);
    }
    
    return null;
}

// ============================================================================
// BELLO API
// ============================================================================

async function sendToBello(itemData) {
    const response = await fetch(`http://localhost:${BELLO_CONFIG.localPort}/connector/save`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            action: 'saveItem',
            data: itemData,
            version: BELLO_CONFIG.version
        })
    });
    
    if (!response.ok) {
        const text = await response.text().catch(() => '');
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }
    
    return await response.json().catch(() => null);
}

// ============================================================================
// NOTIFICATIONS
// ============================================================================

function showNotification(message, type = 'info') {
    try {
        const api = chrome.notifications || (typeof browser !== 'undefined' && browser.notifications);
        if (api && typeof api.create === 'function') {
            api.create({
                type: 'basic',
                iconUrl: 'images/logo.svg',
                title: 'Bello',
                message: message
            });
            return;
        }
    } catch (e) {}
    console.debug('Bello:', message);
}

// ============================================================================
// OTHER HANDLERS
// ============================================================================

async function detectPageTranslators(tab) {
    return [{ id: 'webpage', name: 'Web Page', priority: 100 }];
}

async function handleSaveItem(data, tab) {
    try {
        const result = await sendToBello(data);
        return { success: true, result };
    } catch (error) {
        return { success: false, error: error.message };
    }
}
