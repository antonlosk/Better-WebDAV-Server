package web

import "embed"

//go:embed templates/*.html static/css/*.css static/js/*.js
var FS embed.FS