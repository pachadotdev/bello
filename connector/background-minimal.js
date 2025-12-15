/*
 * Bello Connector - Minimal Background Script
 * A streamlined version that only handles the essential functionality
 */

// Configuration
const BELLO_CONFIG = {
    name: 'Bello',
    version: '1.0.0',
    localPort: 1842  // Default Bello connector port (changed from 23119)
};

// Debug: when true, show a notification with the extracted JSON (truncated)
const DEBUG_SHOW_EXTRACT = true;

// Simple message handler
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    switch(message.action) {
        case 'saveItem':
            handleSaveItem(message.data, sender.tab)
                .then(sendResponse)
                .catch(err => sendResponse({error: err.message}));
            return true; // Keep channel open for async response
            
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
    // Try to save current page
    handleQuickSave(tab);
});

// Simple page save functionality
async function handleQuickSave(tab) {
    try {
        console.debug('Bello: quick save triggered', tab && tab.url);
        // Ask the content script to extract rich metadata from the top frame (frameId: 0)
        chrome.tabs.sendMessage(tab.id, { action: 'extractData' }, { frameId: 0 }, async (response) => {
            console.debug('Bello: extractData response', response);
            if (!response || !response.success) {
                console.warn('Bello: extractor missing or failed; attempting fallback extractor', response && response.error);
                // Attempt a fallback: inject a small script to extract basic metadata
                try {
                    chrome.tabs.executeScript(tab.id, {
                        code: `(() => {
                            try {
                                const getMeta = (sel) => { const el = document.querySelector(sel); return el ? (el.getAttribute('content')||el.textContent) : ''; };
                                const findDOI = () => {
                                    const doiMeta = document.querySelector('meta[name="citation_doi"], meta[name="DOI"]');
                                    if (doiMeta) return (doiMeta.getAttribute('content')||'').replace(/^(doi:|https?:\/\/(dx\.)?doi\.org\/)/i,'');
                                    const m = document.body.textContent.match(/10\.\d{4,9}\/[^\s\)\]\"']+/);
                                    return m ? m[0] : '';
                                };
                                const extractAuthors = () => {
                                    const nodes = Array.from(document.querySelectorAll('meta[name="citation_author"]')).map(n=>n.getAttribute('content')||n.textContent).filter(Boolean);
                                    if (nodes.length) return nodes.join(' and ');
                                    const a = document.querySelector('meta[name="author"]');
                                    return a ? (a.getAttribute('content')||a.textContent) : '';
                                };
                                return {
                                    itemType: 'webpage',
                                    title: (document.title||'').trim(),
                                    url: location.href,
                                    authors: extractAuthors(),
                                    year: (function(){ const d = (document.querySelector('meta[name="citation_publication_date"]')||{}).getAttribute && (document.querySelector('meta[name="citation_publication_date"]').getAttribute('content')||''); const dd = new Date(d); return isNaN(dd)?'':dd.getFullYear().toString();})(),
                                    doi: findDOI(),
                                    isbn: (document.querySelector('meta[name="citation_isbn"]')||{}).getAttribute && (document.querySelector('meta[name="citation_isbn"]').getAttribute('content')||''),
                                    abstract: (document.querySelector('meta[name="description"]')||{}).getAttribute && (document.querySelector('meta[name="description"]').getAttribute('content')||'')
                                };
                            } catch (e) { return {}; }
                        })();`
                    }, async (results) => {
                        if (chrome.runtime.lastError) {
                            console.error('Bello: executeScript failed', chrome.runtime.lastError);
                            showNotification('Failed to extract page data (page may block scripts)', 'error');
                            return;
                        }
                        const data = results && results[0] ? results[0] : null;
                        console.debug('Bello: fallback extractor result', data);
                        if (!data) {
                            showNotification('Could not extract page metadata', 'error');
                            return;
                        }
                        try {
                            const result = await sendToBello(data);
                            console.debug('Bello: sendToBello result (fallback)', result);
                            showNotification('Item saved to Bello!');
                        } catch (err) {
                            console.error('Bello: failed to send to app (fallback)', err);
                            showNotification('Failed to save to Bello: ' + err.message, 'error');
                        }
                    });
                } catch (ex) {
                    console.error('Bello: fallback extractor exception', ex);
                    showNotification('Could not extract page metadata', 'error');
                }
                return;
            }

            try {
                console.debug('Bello: sending data to app', response.data);
                if (DEBUG_SHOW_EXTRACT) {
                    try {
                        const dump = JSON.stringify(response.data);
                        const truncated = dump.length > 700 ? dump.slice(0,700) + '…' : dump;
                        showNotification('Extracted: ' + truncated);
                        console.debug('Bello: extracted payload (truncated)', truncated);
                    } catch (e) {
                        console.debug('Bello: error serializing extract data', e);
                    }
                }
                const result = await sendToBello(response.data);
                console.debug('Bello: sendToBello result', result);
                showNotification('Item saved to Bello!');
            } catch (err) {
                console.error('Bello: failed to send to app', err);
                showNotification('Failed to save to Bello: ' + err.message, 'error');
            }
        });
    } catch (error) {
        console.error('Quick save failed:', error);
        showNotification('Failed to save item', 'error');
    }
}

// Send data to local Bello application
async function sendToBello(itemData) {
    try {
        const response = await fetch(`http://localhost:${BELLO_CONFIG.localPort}/connector/save`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                action: 'saveItem',
                data: itemData,
                version: BELLO_CONFIG.version
            })
        });
        
        if (!response.ok) {
            const text = await response.text().catch(()=>'<no-body>');
            console.error('Bello: non-OK response', response.status, response.statusText, text);
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        const json = await response.json().catch(()=>null);
        console.debug('Bello: app response', json);
        return json;
    } catch (error) {
        // If local connection fails, could fall back to alternative storage
        console.error('Bello: sendToBello error', error);
        throw new Error(`Cannot connect to Bello application: ${error.message}`);
    }
}

// Simple notification system
function showNotification(message, type = 'info') {
    chrome.notifications.create({
        type: 'basic',
        iconUrl: 'images/logo.svg',
        title: 'Bello Connector',
        message: message
    });
}

// Detect available translators for current page (simplified)
async function detectPageTranslators(tab) {
    // For now, just return basic web page translator
    return [{
        id: 'webpage',
        name: 'Web Page',
        priority: 100
    }];
}

// Handle item saving
async function handleSaveItem(data, tab) {
    try {
        const result = await sendToBello(data);
        return { success: true, result };
    } catch (error) {
        return { success: false, error: error.message };
    }
}