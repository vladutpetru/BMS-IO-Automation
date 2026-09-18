function update_table()
{
	httpGetAsync('/get?ro_vars', function(result){
		res = result;
	});	
	list_resp = res.split(",");
	list_lenght = list_resp.length
	while(table.rows.length > 0) {
	  table.deleteRow(0);
	}
	get = '';
	for(var i = 0; i < list_resp.length; i++) {
		get = get.concat('/get?', list_resp[i]);
		console.log(get);
		httpGetAsync(get,function(result){
				res = result;
			});
			
		var row = table.insertRow(i);
		var cell1 = row.insertCell(0);
		var cell2 = row.insertCell(1);
		cell1.innerHTML = list_resp[i];
		cell2.innerHTML = res;
		get="";
	}
}