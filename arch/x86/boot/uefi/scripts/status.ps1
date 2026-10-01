# ====== messaggi di stato ====== #

function Write-BootStatus {
    param(
        [ValidateSet('OK', 'INFO', 'WARN', 'FAIL')][string]$Level,
        [string]$Message
    )
    $colors = @{ OK = 'Green'; INFO = 'White'; WARN = 'Yellow'; FAIL = 'Red' }
    $labels = @{ OK = '[  OK  ]'; INFO = '[ INFO ]'; WARN = '[ WARN ]'; FAIL = '[ FAIL ]' }
    Write-Host $labels[$Level] -ForegroundColor $colors[$Level] -NoNewline
    Write-Host " $Message"
    Write-Host ''
}

# ====== separatore su riga distinta ====== #

function Write-BootSection([string]$Name) {
    Write-Host "# ============ $Name ============ #"
    Write-Host ''
}

