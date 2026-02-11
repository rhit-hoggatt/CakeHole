const express = require('express');
const path = require('path');
const http = require('http');
const session = require('express-session');
const app = express();
const port = 3333;

app.use(session({
    secret: 'your-momma',
    resave: false,
    saveUninitialized: true,
    cookie: { secure: false }
}));

app.use(express.json());

// Serve static files (like index.html) from the 'public' directory
app.use(express.static(path.join(__dirname, 'public')));

// Define a route for the root path ("/")
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'login.html'));
});

// Helper function to call the C API
function callApi(endpoint, method, body, callback) {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: endpoint,
        method: method,
        headers: {
            'Content-Type': 'application/json'
        }
    };

    const req = http.request(options, (res) => {
        let data = '';

        res.on('data', (chunk) => {
            data += chunk;
        });

        res.on('end', () => {
            if (res.statusCode >= 200 && res.statusCode < 300) {
                try {
                    // Try to parse as JSON, otherwise return text
                    const parsed = JSON.parse(data);
                    callback(null, parsed);
                } catch (e) {
                    callback(null, data);
                }
            } else {
                try {
                    const parsed = JSON.parse(data);
                    callback(parsed.error || 'API Error', null);
                } catch (e) {
                    callback('API Error: ' + res.statusCode, null);
                }
            }
        });
    });

    req.on('error', (error) => {
        callback(error, null);
    });

    if (body) {
        req.write(JSON.stringify(body));
    }
    req.end();
}


app.post('/api/login', (req, res) => {
    const { username, password } = req.body;
    callApi('/validateLogin', 'POST', { username, password }, (err, data) => {
        if (err) {
            return res.status(401).json({ success: false, message: 'Invalid credentials' });
        }
        if (data && data.status === 'Login successful') {
            req.session.isAuthenticated = true;
            res.json({ success: true });
        } else {
            res.status(401).json({ success: false, message: 'Invalid credentials' });
        }
    });
});

app.post('/api/logout', (req, res) => {
    req.session.destroy((err) => {
        if (err) {
            return res.status(500).json({ error: 'Failed to log out' });
        }
        res.redirect('/');
    });
});

function isAuthenticated(req, res, next) {
    if (req.session.isAuthenticated) {
        return next();
    }
    res.redirect('/');
}

app.get('/index.html', isAuthenticated, (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

app.get('/api/terminalOutput', (req, res) => {
    callApi('/terminalOutput', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        // The C server returns raw text for this endpoint
        res.json({ output: data });
    });
});

app.get('/api/domainsInAdlist', (req, res) => {
    callApi('/domainsInAdlist', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.get('/api/numQueries', (req, res) => {
    callApi('/numQueries', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/enableSpecificAdlist', (req, res) => {
    callApi('/enableAdlist', 'POST', { url: req.body.url }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/disableSpecificAdlist', (req, res) => {
    callApi('/disableAdlist', 'POST', { url: req.body.url }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/enableAdBlocker', (req, res) => {
    callApi('/enableAdCache', 'POST', {}, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/disableAdBlocker', (req, res) => {
    callApi('/disableAdCache', 'POST', {}, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});


app.post('/api/enableAdlist', (req, res) => {
    callApi('/enableAdCache', 'POST', {}, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/disableAdList', (req, res) => {
    callApi('/disableAdCache', 'POST', {}, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.get('/api/getAdlists', (req, res) => {
    callApi('/getAdlists', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        // The C server returns a raw string of adlists separated by commas
        res.json({ data: data });
    });
});

app.post('/api/addAdlist', (req, res) => {
    callApi('/addAdlist', 'POST', { url: req.body.url }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/removeAdlist', (req, res) => {
    callApi('/removeAdlist', 'POST', { url: req.body.url }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/reloadAdlists', (req, res) => {
    callApi('/reloadAdlists', 'POST', {}, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/restartDNS', (req, res) => {
    callApi('/restartDNS', 'POST', {}, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

const graphData = {
    labels: [],
    queries: [],
    blocked: []
};
const maxDataPoints = 120;

let lastProcessed = null;
let lastBlocked = null;

setInterval(() => {
    callApi('/numQueries', 'GET', null, (err, data) => {
        if (err) return console.error('Error fetching graph data:', err);

        const totalQueries = data.processed;
        const totalBlocked = data.blocked;

        let queriesDiff = 0;
        let blockedDiff = 0;

        if (lastProcessed !== null && lastBlocked !== null) {
            queriesDiff = totalQueries - lastProcessed;
            blockedDiff = totalBlocked - lastBlocked;
        }
        lastProcessed = totalQueries;
        lastBlocked = totalBlocked;

        const now = new Date();
        const timeLabel = now.getHours().toString().padStart(2, '0') + ':' +
            now.getMinutes().toString().padStart(2, '0') + ':' +
            now.getSeconds().toString().padStart(2, '0');

        graphData.labels.push(timeLabel);
        graphData.queries.push(queriesDiff);
        graphData.blocked.push(blockedDiff);

        if (graphData.labels.length > maxDataPoints) {
            graphData.labels.shift();
            graphData.queries.shift();
            graphData.blocked.shift();
        }
    });
}, 5000);

app.get('/api/graphData', (req, res) => {
    res.json(graphData);
});

app.post('/api/addLocalDomain', (req, res) => {
    const { domain, ip, name } = req.body;
    callApi('/addLocalDomain', 'POST', { domain, ip, name }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.get('/api/getLocalDomains', (req, res) => {
    callApi('/getLocalDNSEntries', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        // C server returns newline separated string
        try {
            const domains = data
                .split('\n')
                .filter(line => line.trim() !== '')
                .map(line => {
                    const parts = line.trim().split(/\s+/);
                    return {
                        ip: parts[0] || '',
                        domain: parts[1] || '',
                        name: parts[2] || ''
                    };
                });
            res.json(domains);
        } catch (e) {
            res.status(500).json({ error: 'Failed to parse response' });
        }
    });
});

app.post('/api/deleteLocalDomain', (req, res) => {
    callApi('/removeLocalDomain', 'POST', { domain: req.body.domain }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

// DHCP API routes
app.post('/api/addDhcpLease', (req, res) => {
    const { mac, ip, name } = req.body;
    callApi('/addDhcpLease', 'POST', { mac, ip, name }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.get('/api/getDhcpLeases', (req, res) => {
    callApi('/getDhcpLeases', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        // lease data is usually JSON
        res.json(data);
    });
});

app.post('/api/deleteDhcpLease', (req, res) => {
    callApi('/deleteDhcpLease', 'POST', { mac: req.body.mac }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.get('/api/getDhcpStatus', (req, res) => {
    callApi('/getDhcpStatus', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/itemUpdateKey', (req, res) => {
    // This seems to be for toggling DHCP/DNSSEC status
    const { key, value } = req.body;

    let endpoint = '';
    let body = { enabled: value };

    if (key === 'dhcp') {
        endpoint = '/setDhcpStatus';
    } else if (key === 'dnssec') {
        endpoint = '/setDnssecStatus';
    } else {
        return res.status(400).json({ error: 'Invalid key' });
    }

    callApi(endpoint, 'POST', body, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/setDhcpStatus', (req, res) => {
    // enabled is passed as query param in index.html currently, but we want to move to body.
    // However, index.html might still send it as query param `?enabled=1`.
    // My plan is to update index.html to send JSON body.
    // So here I will expect req.body.enabled.
    callApi('/setDhcpStatus', 'POST', { enabled: req.body.enabled }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/setDnssecStatus', (req, res) => {
    callApi('/setDnssecStatus', 'POST', { enabled: req.body.enabled }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.get('/api/getDhcpSettings', (req, res) => {
    callApi('/getDhcpSettings', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/saveDhcpSettings', (req, res) => {
    callApi('/setDhcpSettings', 'POST', req.body, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.get('/api/getDnssecStatus', (req, res) => {
    callApi('/getDnssecStatus', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.get('/api/getDnssecStats', (req, res) => {
    callApi('/getDnssecStats', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.get('/api/getUpstreamDNS', (req, res) => {
    callApi('/getUpstreamDNS', 'GET', null, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.post('/api/setUpstreamDNS', (req, res) => {
    callApi('/setUpstreamDNS', 'POST', { upstreamDNS: req.body.upstreamDNS }, (err, data) => {
        if (err) return res.status(500).json({ error: err });
        res.json(data);
    });
});

app.listen(port, () => {
    console.log(`Server running at http://localhost:${port}`);
});