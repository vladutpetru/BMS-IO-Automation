function update_table()
{
	httpGetAsync('/get?rw_vars', function(result){
		res = result;
	});
	list_resp = res.split(",");
	list_lenght = list_resp.length
	while(table.rows.length > 0) {
	  table.deleteRow(0);
	}
	set = '';
	if (res.length>0)
	{
		for(var i = 0; i < list_resp.length; i++) {
		console.log(res.length);
		var row = table.insertRow(i);
		var cell1 = row.insertCell(0);
		var cell2 = row.insertCell(1);
		set = set.concat('/set?', list_resp[i]);
		cell1.innerHTML = list_resp[i];
		let var_name = list_resp[i];
		console.log(var_name);
		cell2.innerHTML = '<input type="number" type="text" id="'+i+'" size="10">';
		set="";
		}
	}
}