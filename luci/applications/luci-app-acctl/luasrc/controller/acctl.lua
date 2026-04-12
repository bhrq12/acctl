module('luci.controller.acctl', package.seeall)

function index()
    entry({'admin', 'network', 'acctl'}, cbi('acctl'), _('AC Controller'), 60)
    entry({'admin', 'network', 'acctl', 'status'}, call('action_status'), _('Status'), 10)
end

function action_status()
    local sys = require 'luci.sys'
    local json = require 'luci.jsonc'
    local status = {}
    
    status.running = sys.call('pgrep acser > /dev/null') == 0
    status.ap_count = tonumber(sys.exec('ls /etc/acctl/*.json 2>/dev/null | wc -l')) or 0
    
    luci.http.prepare_content('application/json')
    luci.http.write_json(status)
end
