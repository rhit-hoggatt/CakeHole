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

app.post('/api/login', (req, res) => {
    const { username, password } = req.body;

    const options = {
        hostname: 'localhost',
        port: 8081,
        path: `/validateLogin?username=${encodeURIComponent(username)}&password=${encodeURIComponent(password)}`,
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };

    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                if (parsedData.status === 'Login successful') {
                    req.session.isAuthenticated = true; // Mark the user as authenticated
                    res.json({ success: true });
                } else {
                    res.status(401).json({ success: false, message: 'Invalid credentials' });
                }
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });

    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });

    request.write(JSON.stringify({ username, password }));
    request.end();
});

app.post('/api/logout', (req, res) => {
    req.session.destroy((err) => {
        if (err) {
            return res.status(500).json({ error: 'Failed to log out' });
        }
        res.redirect('/'); // Redirect to the login page
    });
});

function isAuthenticated(req, res, next) {
    if (req.session.isAuthenticated) {
        return next(); // User is authenticated, proceed to the next middleware
    }
    res.redirect('/'); // Redirect to the login page if not authenticated
}

app.get('/index.html', isAuthenticated, (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

app.get('/api/terminalOutput', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/terminalOutput',
        method: 'GET'
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                res.json({ output: data });
            } catch (error) {
                res.status(502).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.get('/api/domainsInAdlist', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/domainsInAdlist',
        method: 'GET'
    };

    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });

    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });

    request.end();
});

app.get('/api/numQueries', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/numQueries',
        method: 'GET'
    };

    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });

    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });

    request.end();
});

app.post('/api/enableSpecificAdlist', (req, res) => {
    const urlToSend = req.body.url; // Extract the URL from the POST body

    const options = {
        hostname: 'localhost',
        port: 8081,
        path: `/enableAdlist?url=${encodeURIComponent(urlToSend)}`, // Append as a query parameter
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    }
    );
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    }
    );
    request.end();
});

app.post('/api/disableSpecificAdlist', (req, res) => {
    const urlToSend = req.body.url; // Extract the URL from the POST body

    const options = {
        hostname: 'localhost',
        port: 8081,
        path: `/disableAdlist?url=${encodeURIComponent(urlToSend)}`, // Append as a query parameter
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    }
    );
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    }
    );
    request.end();
});

app.post('/api/enableAdlist', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/enableAdCache',
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.write(JSON.stringify({ enable: true })); // Send the request body
    request.end();
});

app.post('/api/disableAdList', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/disableAdCache',
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.write(JSON.stringify({ enable: true })); // Send the request body
    request.end();
});

app.get('/api/getAdlists', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/getAdlists',
        method: 'GET'
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const dataJSON = {"data": data};
                res.json(dataJSON);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.post('/api/addAdlist', (req, res) => {
    const urlToSend = req.body.url; // Extract the URL from the POST body

    const options = {
        hostname: 'localhost',
        port: 8081,
        path: `/addAdlist?url=${encodeURIComponent(urlToSend)}`, // Append as a query parameter
        method: 'GET', // Change the method to GET
        headers: {
            // 'Content-Type': 'application/json' - Not needed for GET with query params
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });

    request.end(); // No need for request.write with GET
});

app.post('/api/removeAdlist', (req, res) => {
    const urlToSend = req.body.url; // Extract the URL from the POST body

    const options = {
        hostname: 'localhost',
        port: 8081,
        path: `/removeAdlist?url=${encodeURIComponent(urlToSend)}`, // Append as a query parameter
        method: 'GET', // Change the method to GET
        headers: {
            // 'Content-Type': 'application/json' - Not needed for GET with query params
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });

    request.end(); // No need for request.write with GET
});

app.post('/api/enableAdlist', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/enableAdlist',
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.write(JSON.stringify({ url: req.body.url })); // Send the request body
    request.end();
});

app.post('/api/disableAdlist', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/disableAdlist',
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.write(JSON.stringify({ url: req.body.url })); // Send the request body
    request.end();
});

app.post('/api/reloadAdlists', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/reloadAdlists',
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    }
    );
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    }
    );
    request.end();
});

app.post('/api/restartDNS', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/restartDNS',
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    }
    );
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    }
    );
    request.end();
});

const graphData = {
    labels: [], // Time labels
    queries: [], // Queries in this interval
    blocked: [] // Blocked queries in this interval
};
const maxDataPoints = 120;

let lastProcessed = null;
let lastBlocked = null;

setInterval(() => {
    fetch('http://localhost:3333/api/numQueries')
        .then(response => response.json())
        .then(data => {
            const totalQueries = data.processed;
            const totalBlocked = data.blocked;

            // Calculate the difference since last poll
            let queriesDiff = 0;
            let blockedDiff = 0;
            if (lastProcessed !== null && lastBlocked !== null) {
                queriesDiff = totalQueries - lastProcessed;
                blockedDiff = totalBlocked - lastBlocked;
            }
            lastProcessed = totalQueries;
            lastBlocked = totalBlocked;

            // Add data to graphData
            const now = new Date();
            const timeLabel = now.getHours().toString().padStart(2, '0') + ':' +
                              now.getMinutes().toString().padStart(2, '0') + ':' +
                              now.getSeconds().toString().padStart(2, '0');

            graphData.labels.push(timeLabel);
            graphData.queries.push(queriesDiff);
            graphData.blocked.push(blockedDiff);

            // Keep the graph data within the last hour
            if (graphData.labels.length > maxDataPoints) {
                graphData.labels.shift();
                graphData.queries.shift();
                graphData.blocked.shift();
            }
        })
        .catch(error => console.error('Error fetching data:', error));
}, 60000 * 5);

app.get('/api/graphData', (req, res) => {
    res.json(graphData);
});

app.post('/api/addLocalDomain', (req, res) => {
    const domainToSend = req.body.domain;
    const ipToSend = req.body.ip;
    const nameToSend = req.body.name;

    const options = {
        hostname: 'localhost',
        port: 8081,
        path: `/addLocalDomain?domain=${encodeURIComponent(domainToSend)}&ip=${encodeURIComponent(ipToSend)}&name=${encodeURIComponent(nameToSend)}`,
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.get('/api/getLocalDomains', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/getLocalDNSEntries',
        method: 'GET'
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
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
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.post('/api/deleteLocalDomain', (req, res) => {
    const domainToSend = req.body.domain;

    const options = {
        hostname: 'localhost',
        port: 8081,
        path: `/removeLocalDomain?domain=${encodeURIComponent(domainToSend)}`,
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.get('/api/getNumThreads', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/getNumThreads',
        method: 'GET'
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.post('/api/setNumThreads', (req, res) => {
    const numThreadsToSend = req.body.numThreads;

    const options = {
        hostname: 'localhost',
        port: 8081,
        path: `/setNumThreads?numThreads=${encodeURIComponent(numThreadsToSend)}`,
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    }
    );
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    }
    );
    request.end();
});

app.get('/api/getAvgCacheLookupTime', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/getAvgCacheLookupTime',
        method: 'GET'
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.get('/api/getAvgCacheResponseTime', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/getAvgCacheResponseTime',
        method: 'GET'
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.get('/api/getAvgNonCachedResponseTime', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/getAvgNonCachedResponseTime',
        method: 'GET'
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.get('/api/getUpstreamDNS', (req, res) => {
    const options = {
        hostname: 'localhost',
        port: 8081,
        path: '/getUpstreamDNS',
        method: 'GET'
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.post('/api/setUpstreamDNS', (req, res) => {
    const upstreamDNSToSend = req.body.upstreamDNS;

    const options = {
        hostname: 'localhost',
        port: 8081,
        path: `/setUpstreamDNS?upstreamDNS=${encodeURIComponent(upstreamDNSToSend)}`,
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        }
    };
    const request = http.request(options, (response) => {
        let data = '';

        response.on('data', (chunk) => {
            data += chunk;
        });

        response.on('end', () => {
            try {
                const parsedData = JSON.parse(data);
                res.json(parsedData);
            } catch (error) {
                res.status(500).json({ error: 'Failed to parse response from C server' });
            }
        });
    });
    request.on('error', (error) => {
        res.status(500).json({ error: 'Failed to communicate with C server' });
    });
    request.end();
});

app.listen(port, () => {
    console.log(`Server listening on port ${port}`);
});

app.use((req, res, next) => {
    res.status(404);
    res.sendFile(path.join(__dirname, '404.html'), err => {
        if (err) {
            res.type('txt').send('404: Page Not Found');
        }
    });
});