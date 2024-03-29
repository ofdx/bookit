/*
	BookIt!
	mperron (2024)
*/

var todaysDate = new Date().toLocaleDateString();

// Replace all Unix seconds timestamps on the page with human-readable dates.
document.querySelectorAll("[class=utctime]").forEach(el => {
	let unixtime = parseInt(el.innerHTML);

	if(isNaN(unixtime))
		return;

	let convert = function(){
		let d = new Date(unixtime * 1000);
		let date = d.toLocaleDateString();

		el.innerHTML = ((date === todaysDate) ? "" : (date + " ")) + d.toLocaleTimeString([], {
			hour: '2-digit',
			minute: '2-digit'
		});
	};

	convert();

	// If this is the clock, increment it every minute.
	if(el.id == 'clock'){
		setInterval(function(){
			todaysDate = new Date().toLocaleDateString();
			unixtime += 60;
			convert();
		}, (60  * 1000));
	}
});

// Preserve the value set for m_info (name/email of reserver).
if(localStorage){
	var infofield = document.getElementById('m_info');

	if(infofield){
		infofield.value = localStorage.getItem('bookit_m_info');

		infofield.addEventListener('change', el => {
			localStorage.setItem('bookit_m_info', infofield.value);
		});
	}
}

// Callback used by quick time picking buttons.
var durationfield = document.getElementById('m_duration');
if(durationfield){
	document.querySelectorAll('button').forEach(el => {
		el.addEventListener('click', (e) => {
			e.preventDefault();
			durationfield.value = el.getAttribute('duration');
		});
	});
}

// Form validation.
var reserverform = document.getElementById('reserver');
var reserverform_inflight = false;
if(reserverform){
	var disableform = function(){
		for(var i = 0, len = reserverform.length; i < len; ++ i){
			reserverform[i].disabled = true;
		}
	}

	var enableform = function(notsubmit){
		for(var i = 0, len = reserverform.length; i < len; ++ i){
			if(notsubmit && (reserverform[i].type === 'submit'))
				continue;

			reserverform[i].disabled = false;
		}
	}

	reserverform.addEventListener('submit', (e) => {
		disableform();

		let durationfield = reserverform['m_duration'];
		let infofield = reserverform['m_info'];

		if(durationfield){
			let duration = parseInt(durationfield.value);

			if(isNaN(duration)){
				e.preventDefault();
				alert("Please enter a number of minutes to reserve the cluster, or click one of the quick buttons.");
				enableform();
				return;
			}
			if(duration < 15){
				e.preventDefault();
				alert("You should reserve the cluster for at least 15 minutes.");
				enableform();
				return;
			}
			if(duration > (60 * 24)){
				e.preventDefault();
				alert("Please enter a time less than 1440 minutes (24 hours).");
				enableform();
				return;
			}
		}

		if(infofield && !infofield.value){
			e.preventDefault();
			alert("Please enter your name or email alias.");
			enableform();
			return;
		}

		enableform(true);
	});
}

