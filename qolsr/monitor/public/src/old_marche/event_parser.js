(function(){
  
  Event_parser = function(){

    var that = {
      eparse:function(s){
      //  {"v":"1.0","ts":1407945088840,"type":"verdict","data":{"id":"LinkSpoofing.verdict","value":false,"instance_id":"192.168.200.255"},
      //      "attributes":{"orig":"192.168.200.2","neighbor":"192.168.200.5","type":10,"attacker":"192.168.200.2"}}
        obj = JSON.parse(s)
        console.log(obj.v)
        return obj
      }
    }
    
    return that
  }

  

  
})()