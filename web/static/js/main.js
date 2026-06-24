// Simple client side search for logs
document.addEventListener('DOMContentLoaded', () => {
    const searchInput = document.getElementById('logSearch');
    if (searchInput) {
        searchInput.addEventListener('keyup', function() {
            let filter = this.value.toLowerCase();
            let rows = document.querySelectorAll('#logsTable tr:not(:first-child)');
            rows.forEach(row => {
                let text = row.textContent.toLowerCase();
                row.style.display = text.includes(filter) ? '' : 'none';
            });
        });
    }
});