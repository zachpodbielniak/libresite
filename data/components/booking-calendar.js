/* The host owns availability and booking. This script only draws its public
 * slots and returns to the ordinary booking page when the host is unavailable. */
document.querySelectorAll('.booking-calendar [data-booking-url]').forEach(function(root){
  var url=root.dataset.bookingUrl, app=root.querySelector('.booking-calendar__app');
  var fallback=root.querySelector('.booking-calendar__fallback');
  var days=root.querySelector('[data-booking-days]'), monthLabel=root.querySelector('[data-booking-month]');
  var status=root.querySelector('[data-booking-status]'), times=root.querySelector('[data-booking-times]');
  var timeList=root.querySelector('[data-booking-time-list]'), form=root.querySelector('[data-booking-form]');
  var confirmed=root.querySelector('[data-booking-confirmed]'), slots=[], selected='', offset=0;
  var today=new Date(), first=new Date(today.getFullYear(),today.getMonth(),1);
  function dateLabel(day){return new Date(day+'T12:00:00').toLocaleDateString(undefined,{weekday:'long',month:'long',day:'numeric'});}
  function showMonth(){
    var current=new Date(first.getFullYear(),first.getMonth()+offset,1), count=new Date(current.getFullYear(),current.getMonth()+1,0).getDate();
    var prefix=current.getDay(), i, day, button, available;
    monthLabel.textContent=current.toLocaleDateString(undefined,{month:'long',year:'numeric'});
    days.replaceChildren();
    for(i=0;i<prefix;i++)days.appendChild(document.createElement('span'));
    for(i=1;i<=count;i++){
      day=current.getFullYear()+'-'+String(current.getMonth()+1).padStart(2,'0')+'-'+String(i).padStart(2,'0');
      available=slots.some(function(slot){return slot.day===day;});
      button=document.createElement('button');button.type='button';button.textContent=String(i);
      button.dataset.day=day;button.disabled=!available;button.setAttribute('aria-label',dateLabel(day)+(available?' — open times':' — no open times'));
      button.setAttribute('aria-pressed',day===selected?'true':'false');days.appendChild(button);
    }
    root.querySelector('[data-booking-prev]').disabled=offset<=0;
    root.querySelector('[data-booking-next]').disabled=!slots.some(function(slot){return slot.day.slice(0,7)>current.getFullYear()+'-'+String(current.getMonth()+1).padStart(2,'0');});
  }
  function chooseDay(day){
    selected=day;showMonth();timeList.replaceChildren();form.hidden=true;times.hidden=false;
    root.querySelector('[data-booking-day-title]').textContent=dateLabel(day);
    slots.filter(function(slot){return slot.day===day;}).forEach(function(slot){
      var button=document.createElement('button');button.type='button';button.textContent=slot.label;
      button.dataset.start=slot.start;button.setAttribute('aria-pressed','false');timeList.appendChild(button);
    });
    status.textContent='Choose an open time.';
  }
  days.addEventListener('click',function(event){var button=event.target.closest('button[data-day]');if(button)chooseDay(button.dataset.day);});
  timeList.addEventListener('click',function(event){
    var button=event.target.closest('button[data-start]');if(!button)return;
    timeList.querySelectorAll('button').forEach(function(item){item.setAttribute('aria-pressed',item===button?'true':'false');});
    form.elements.start.value=button.dataset.start;root.querySelector('[data-booking-picked]').textContent=dateLabel(selected)+' at '+button.textContent;
    form.hidden=false;status.textContent='Enter your name and email to confirm.';form.elements.name.focus();
  });
  function moveMonth(delta){offset+=delta;selected='';times.hidden=true;form.hidden=true;showMonth();status.textContent='Choose an open day.';}
  root.querySelector('[data-booking-prev]').addEventListener('click',function(){moveMonth(-1);});
  root.querySelector('[data-booking-next]').addEventListener('click',function(){moveMonth(1);});
  form.addEventListener('submit',function(event){
    var submit=form.querySelector('[type=submit]'), fields=new FormData(form);
    event.preventDefault();submit.disabled=true;status.textContent='Confirming your booking…';
    fetch(url,{method:'POST',headers:{'Content-Type':'application/json','Accept':'application/json'},
      body:JSON.stringify({start:fields.get('start'),name:fields.get('name'),email:fields.get('email'),notes:fields.get('notes')})})
      .then(function(reply){if(!reply.ok)throw new Error(reply.status===409?'That time was just taken. Choose another.':'Booking could not be confirmed. Please try again.');return reply.json();})
      .then(function(){form.hidden=true;times.hidden=true;days.hidden=true;root.querySelector('.booking-calendar__nav').hidden=true;
        root.querySelector('.booking-calendar__weekdays').hidden=true;confirmed.textContent='Your booking is confirmed. Check your email for the details.';
        confirmed.hidden=false;confirmed.focus();status.textContent='';})
      .catch(function(error){
        if(error.message==='That time was just taken. Choose another.'){
          slots=slots.filter(function(slot){return slot.start!==form.elements.start.value;});
          chooseDay(selected);
        }
        status.textContent=error.message;submit.disabled=false;
      });
  });
  fetch(url+(url.indexOf('?')<0?'?':'&')+'format=json',{headers:{'Accept':'application/json'}})
    .then(function(reply){if(!reply.ok)throw new Error('Open times are unavailable.');return reply.json();})
    .then(function(data){if(!data||!Array.isArray(data.slots))throw new Error('Open times are unavailable.');
      slots=data.slots.filter(function(slot){return slot&&/^\d{4}-\d\d-\d\d$/.test(slot.day)&&typeof slot.start==='string'&&typeof slot.label==='string';});
      app.hidden=false;fallback.hidden=true;showMonth();status.textContent=slots.length?'Choose an open day.':'No open times are available right now.';})
    .catch(function(){fallback.hidden=false;});
});
