function httpGetAsync(theUrl, callback) {
	console.log(theUrl);
  let xmlHttpReq = new XMLHttpRequest();
  xmlHttpReq.onreadystatechange = function () {
	if (xmlHttpReq.readyState == 4 && xmlHttpReq.status == 200)
	  callback(xmlHttpReq.responseText);
  };
  xmlHttpReq.open("GET", theUrl, false);
  xmlHttpReq.send(null);
}